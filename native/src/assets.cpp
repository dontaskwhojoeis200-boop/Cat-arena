#include "cat/assets.hpp"
#include "cat/utf8_path.hpp"
#include <algorithm>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
namespace cat::assets {
namespace {
constexpr std::size_t max_bytes=256U*1024U*1024U,max_nodes=4U*1024U*1024U;
[[noreturn]] void fail(const std::string& text){throw std::runtime_error("asset: "+text);}
void need(bool condition,const std::string& text){if(!condition)fail(text);}
class Reader {
    std::span<const std::uint8_t> bytes_;std::size_t cursor_{};
public:
    explicit Reader(std::span<const std::uint8_t> bytes):bytes_(bytes){need(bytes.size()<=max_bytes,"file exceeds budget");}
    std::size_t left() const {return bytes_.size()-cursor_;}
    std::span<const std::uint8_t> take(std::size_t n){need(n<=left(),"truncated payload");auto r=bytes_.subspan(cursor_,n);cursor_+=n;return r;}
    std::uint8_t byte(){return take(1)[0];}
    std::uint32_t u32(){auto b=take(4);return std::uint32_t(b[0])|(std::uint32_t(b[1])<<8)|(std::uint32_t(b[2])<<16)|(std::uint32_t(b[3])<<24);}
    double f64(){std::uint64_t bits=0;auto b=take(8);for(unsigned i=0;i<8;++i)bits|=std::uint64_t(b[i])<<(8*i);double n=std::bit_cast<double>(bits);need(std::isfinite(n),"nonfinite number");return n;}
    void magic(std::string_view magic){auto b=take(magic.size());need(std::memcmp(b.data(),magic.data(),b.size())==0,"invalid magic");}
};
Value read_value(Reader& r,unsigned depth,std::size_t& nodes){
    need(depth<=64 && ++nodes<=max_nodes,"value budget exceeded");
    switch(r.byte()){
    case 0:return Value{};
    case 1:return Value(false);
    case 2:return Value(true);
    case 3:return Value(r.f64());
    case 4:{const auto n=r.u32();auto b=r.take(n);return Value(std::string(reinterpret_cast<const char*>(b.data()),b.size()));}
    case 5:{auto n=r.u32();need(n<=r.left()&&n<=max_nodes-nodes,"array budget exceeded");Value::Array a;a.reserve(n);for(std::uint32_t i=0;i<n;++i)a.push_back(read_value(r,depth+1,nodes));return Value(std::move(a));}
    case 6:{auto n=r.u32();need(n<=r.left()/6&&n<=max_nodes-nodes,"object budget exceeded");Value::Object object;for(std::uint32_t i=0;i<n;++i){auto key=read_value(r,depth+1,nodes).string();auto value=read_value(r,depth+1,nodes);need(object.emplace(key,std::move(value)).second,"duplicate object key");}return Value(std::move(object));}
    default:fail("unknown value tag");
    }
}
void u32(std::vector<std::uint8_t>& out,std::uint32_t v){for(unsigned i=0;i<4;++i)out.push_back(static_cast<std::uint8_t>(v>>(i*8)));}
std::uint32_t count(std::size_t n){need(n<=max_bytes,"output exceeds budget");return static_cast<std::uint32_t>(n);}
void write_value(std::vector<std::uint8_t>& out,const Value& v,unsigned depth){
    need(depth<=64 && out.size()<=max_bytes,"writer budget exceeded");
    if(std::holds_alternative<std::nullptr_t>(v.data)){out.push_back(0);return;}
    if(const auto* b=std::get_if<bool>(&v.data)){out.push_back(*b?2:1);return;}
    if(const auto* n=std::get_if<double>(&v.data)){need(std::isfinite(*n),"nonfinite output");out.push_back(3);auto bits=std::bit_cast<std::uint64_t>(*n);for(unsigned i=0;i<8;++i)out.push_back(static_cast<std::uint8_t>(bits>>(i*8)));return;}
    if(const auto* s=std::get_if<std::string>(&v.data)){out.push_back(4);u32(out,count(s->size()));out.insert(out.end(),s->begin(),s->end());return;}
    if(const auto* a=std::get_if<Value::Array>(&v.data)){out.push_back(5);u32(out,count(a->size()));for(const auto& child:*a)write_value(out,child,depth+1);return;}
    const auto& object=v.object();out.push_back(6);u32(out,count(object.size()));for(const auto& [key,child]:object){write_value(out,Value(key),depth+1);write_value(out,child,depth+1);}
}
std::vector<std::uint8_t> read_file(const std::filesystem::path& path){
    std::ifstream f(path,std::ios::binary|std::ios::ate);need(bool(f),"cannot open "+path_utf8(path));auto size=f.tellg();need(size>=0&&static_cast<std::uint64_t>(size)<=max_bytes,"invalid file size");
    std::vector<std::uint8_t> b(static_cast<std::size_t>(size));f.seekg(0);f.read(reinterpret_cast<char*>(b.data()),static_cast<std::streamsize>(b.size()));need(bool(f),"read failed");return b;
}
std::size_t integer(const Value& v){double n=v.number();need(n>=0&&n<=double(max_bytes)&&std::floor(n)==n,"invalid count/index");return static_cast<std::size_t>(n);}
void vector(const Value& v,std::size_t n){need(v.array().size()==n,"vector dimension");for(const auto& x:v.array())(void)x.number();}
void polygon(const Value& v){need(v.array().size()>=3,"polygon has fewer than 3 points");for(const auto& p:v.array())vector(p,2);}
void mesh_valid(const Mesh& m){
    need(m.stride==12||m.stride==14||m.stride==18,"unsupported vertex stride");need(m.vertices.size()%m.stride==0&&m.indices.size()%3==0,"triangle alignment");
    for(float f:m.vertices) { need(std::isfinite(f),"nonfinite vertex"); }
    for(auto i:m.indices) { need(i<m.vertex_count(),"mesh index out of bounds"); }
}
}
const Value& Value::at(std::string_view k) const {const auto& o=object();auto i=o.find(k);if(i==o.end())fail("missing field "+std::string(k));return i->second;}
const Value::Array& Value::array() const {auto p=std::get_if<Array>(&data);if(!p)fail("expected array");return *p;}
const Value::Object& Value::object() const {auto p=std::get_if<Object>(&data);if(!p)fail("expected object");return *p;}
const std::string& Value::string() const {auto p=std::get_if<std::string>(&data);if(!p)fail("expected string");return *p;}
double Value::number() const {auto p=std::get_if<double>(&data);if(!p||!std::isfinite(*p))fail("expected finite number");return *p;}
bool Value::boolean() const {auto p=std::get_if<bool>(&data);if(!p)fail("expected boolean");return *p;}
std::uint32_t crc32(std::span<const std::uint8_t> b){std::uint32_t c=0xffffffff;for(auto v:b){c^=v;for(int i=0;i<8;++i)c=(c>>1)^((c&1)?0xedb88320:0);}return c^0xffffffff;}
Value decode_data(std::span<const std::uint8_t> b){Reader r(b);r.magic("CATDATA1");need(r.u32()==1,"unsupported data version");const auto n=r.u32(),crc=r.u32();need(n==r.left(),"payload size/trailing bytes");auto payload=r.take(n);need(crc32(payload)==crc,"data checksum mismatch");Reader p(payload);std::size_t nodes=0;auto result=read_value(p,0,nodes);need(p.left()==0,"trailing value bytes");return result;}
std::vector<std::uint8_t> encode_data(const Value& v){std::vector<std::uint8_t> payload;write_value(payload,v,0);std::vector<std::uint8_t> out{'C','A','T','D','A','T','A','1'};u32(out,1);u32(out,count(payload.size()));u32(out,crc32(payload));out.insert(out.end(),payload.begin(),payload.end());need(out.size()<=max_bytes,"output exceeds budget");return out;}
Mesh decode_mesh(std::span<const std::uint8_t> b){
    Reader r(b);r.magic("CATMESH1");need(r.u32()==1,"unsupported mesh version");Mesh m;m.stride=r.u32();auto vertices=r.u32(),indices=r.u32(),crc=r.u32();
    need(m.stride==12||m.stride==14||m.stride==18,"unsupported mesh stride");
    const std::uint64_t floats=std::uint64_t(vertices)*m.stride,total=(floats+indices)*4;
    need(total==r.left()&&total<=max_bytes,"mesh length/overflow");auto payload=r.take(static_cast<std::size_t>(total));need(crc32(payload)==crc,"mesh checksum mismatch");Reader p(payload);
    m.vertices.reserve(static_cast<std::size_t>(floats));m.indices.reserve(indices);
    for(std::uint64_t i=0;i<floats;++i) { m.vertices.push_back(std::bit_cast<float>(p.u32())); }
    for(std::uint32_t i=0;i<indices;++i) { m.indices.push_back(p.u32()); }
    mesh_valid(m);return m;
}
std::vector<std::uint8_t> encode_mesh(const Mesh& m){mesh_valid(m);need(m.vertices.size()+m.indices.size()<=(max_bytes-28)/4,"mesh writer budget");std::vector<std::uint8_t> b{'C','A','T','M','E','S','H','1'},body;u32(b,1);u32(b,m.stride);u32(b,count(m.vertex_count()));u32(b,count(m.indices.size()));for(auto f:m.vertices)u32(body,std::bit_cast<std::uint32_t>(f));for(auto i:m.indices)u32(body,i);u32(b,crc32(body));b.insert(b.end(),body.begin(),body.end());return b;}
Value load_data(const std::filesystem::path& p){return decode_data(read_file(p));}
Mesh load_mesh(const std::filesystem::path& p){return decode_mesh(read_file(p));}
std::filesystem::path content_path(const std::filesystem::path& root,std::string_view rel){
    const std::filesystem::path p(rel);need(!rel.empty()&&!p.is_absolute()&&!p.has_root_name()&&!p.has_root_directory(),"absolute/empty asset reference");   // "/x" is not absolute on Windows
    need(rel.find('\\')==std::string_view::npos&&rel.find(':')==std::string_view::npos&&rel.find('\0')==std::string_view::npos,"invalid asset reference");
    for(const auto& part:p) { need(part!="..","asset path traversal"); }
    auto base=std::filesystem::weakly_canonical(root),target=std::filesystem::weakly_canonical(base/p);
    auto a=base.begin(),b=target.begin();for(;a!=base.end();++a,++b)need(b!=target.end()&&*a==*b,"asset outside root");return target;
}
World validate_world(Value d){
    need(integer(d.at("schema"))==1,"world schema");World w;w.id=d.at("id").string();
    need(w.id=="helios-crucible"||w.id=="bastion-keep"||w.id=="reactor-bastion"||w.id=="q3-gallery","unknown map id");
    need(d.at("wireId").string()==w.id+"-v1"&&d.at("build").string()=="VA-MODULAR-RB-011","wire identity mismatch");
    const auto& authored=d.at("authored");const auto& compiled=d.at("compiled");
    const auto& slabs=compiled.at("slabs").array();w.slabs=slabs.size();w.walls=compiled.at("walls").array().size();w.spawns=compiled.at("spawns").array().size();w.kits=authored.at("kits").array().size();need(w.slabs>0&&w.spawns>0,"empty map");
    for(const auto& slab:slabs){polygon(slab.at("poly"));(void)slab.at("z").number();need(slab.at("thickness").number()>=0,"negative slab thickness");
        if(!std::holds_alternative<std::nullptr_t>(slab.at("plane").data)){++w.ramps;need(slab.at("plane").at("len").number()>0,"ramp length");}
    }
    for(const auto& wall:compiled.at("walls").array()){for(auto k:{"ax","ay","bx","by","z0","z1"})(void)wall.at(k).number();need(wall.at("z1").number()>wall.at("z0").number(),"inverted wall");}
    for(const auto& spawn:compiled.at("spawns").array())for(auto k:{"x","y","z","a"})(void)spawn.at(k).number();
    // Every explicit identity reference must resolve to the original authored object.
    auto refs=[&](auto&& self,const Value& v)->void {
        if(const auto* o=std::get_if<Value::Object>(&v.data)){
            if(auto ref=o->find("$ref");ref!=o->end()){
                const auto& s=ref->second.string();if(s!="authored"){
                    need(s.starts_with("authored."),"bad source reference");auto open=s.find('['),close=s.find(']');need(open!=std::string::npos&&close==s.size()-1,"reference syntax");
                    const auto& a=authored.at(s.substr(9,open-9)).array();std::size_t i=0;auto [p,ec]=std::from_chars(s.data()+open+1,s.data()+close,i);need(ec==std::errc{}&&p==s.data()+close&&i<a.size(),"unresolved source reference");
                }
            }
            for(const auto& [key,child]:*o){(void)key;self(self,child);}
        }else if(const auto* a=std::get_if<Value::Array>(&v.data))for(const auto& child:*a)self(self,child);
    };refs(refs,compiled);
    const auto gridCount=integer(compiled.at("gw"))*integer(compiled.at("gw"));
    for(auto key:{"slabGrid","solidGrid","cavityGrid","regionGrid"}){
        const auto& grid=compiled.at(key).array();need(grid.size()==gridCount,"grid size");const auto limit=key==std::string_view("slabGrid")?w.slabs:key==std::string_view("solidGrid")?compiled.at("solids").array().size():key==std::string_view("cavityGrid")?compiled.at("cavities").array().size():compiled.at("regions").array().size();
        for(const auto& cell:grid)for(const auto& i:cell.array())need(integer(i)<limit,"grid index out of bounds");
    }
    for(const auto& i:d.at("navigation").at("walkableSlabIndices").array())need(integer(i)<w.slabs&&slabs[integer(i)].at("nav").boolean(),"invalid navigation surface");
    const auto& lm=d.at("lightmap");w.lightmap_compatible=lm.at("compatible").boolean();
    need(w.lightmap_compatible==(lm.at("hash").string()==lm.at("computedHash").string()),"lightmap compatibility claim mismatch");
    w.document=std::move(d);return w;
}
World load_world(const std::filesystem::path& p){return validate_world(load_data(p));}
void validate_render_assets(const World& w,const std::filesystem::path& root){
    const auto& render=w.document.at("render");std::size_t materialIndices=0;
    for(auto key:{"world","sky","materialMesh"}){
        const auto& metadata=render.at(key);auto m=load_mesh(content_path(root,metadata.at("file").string()));
        need(m.vertex_count()==integer(metadata.at("vertices"))&&m.indices.size()==integer(metadata.at("indices"))&&m.stride==integer(metadata.at("stride")),"mesh metadata mismatch");
        if(key==std::string_view("materialMesh"))materialIndices=m.indices.size();
    }
    std::size_t cursor=0;for(const auto& group:render.at("materials").array()){
        auto first=integer(group.at("firstIndex")),n=integer(group.at("indexCount"));need(first==cursor&&n%3==0&&first<=materialIndices&&n<=materialIndices-first,"material range");cursor+=n;
    }need(cursor==materialIndices,"material ranges do not cover mesh");
}
}

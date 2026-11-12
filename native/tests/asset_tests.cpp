#include "cat/assets.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace cat::assets;
void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
template<class F> void reject(F f){bool failed=false;try{f();}catch(const std::exception&){failed=true;}check(failed,"invalid asset accepted");}
void put32(std::vector<std::uint8_t>& b,std::size_t at,std::uint32_t n){for(unsigned i=0;i<4;++i)b.at(at+i)=static_cast<std::uint8_t>(n>>(8*i));}
void repair_data_crc(std::vector<std::uint8_t>& b){put32(b,16,crc32(std::span(b).subspan(20)));}
int main(){try{
 Value tree(Value::Object{{"flag",Value(true)},{"nested",Value(Value::Array{Value(),Value(3.25),Value(std::string("marine"))})}});
 auto bytes=encode_data(tree);check(decode_data(bytes)==tree,"data roundtrip");check(encode_data(decode_data(bytes))==bytes,"canonical rewrite");
 for(std::size_t n=0;n<bytes.size();++n)reject([&]{decode_data(std::span(bytes).first(n));});
 auto bad=bytes;bad.back()^=1;reject([&]{decode_data(bad);});
 bad=bytes;bad[8]=2;reject([&]{decode_data(bad);});
 bad=bytes;bad.push_back(0);reject([&]{decode_data(bad);});
 bad=bytes;bad[20]=255;repair_data_crc(bad);reject([&]{decode_data(bad);});
 bad=encode_data(Value(Value::Array{}));put32(bad,21,0xffffffff);repair_data_crc(bad);reject([&]{decode_data(bad);});
 reject([]{encode_data(Value(std::numeric_limits<double>::infinity()));});
 Value deep;for(int i=0;i<70;++i)deep=Value(Value::Array{std::move(deep)});reject([&]{encode_data(deep);});
 std::cout<<"PASS data: roundtrip, every truncation, checksum, version, trailing bytes, tag, allocation budget, finite values, depth\n";
 Mesh m;m.stride=12;m.vertices.resize(36,0);m.vertices[3]=m.vertices[15]=m.vertices[27]=1;m.indices={0,1,2};
 auto meshBytes=encode_mesh(m);auto decoded=decode_mesh(meshBytes);check(decoded.vertices==m.vertices&&decoded.indices==m.indices,"mesh roundtrip");
 for(std::size_t n=0;n<meshBytes.size();++n)reject([&]{decode_mesh(std::span(meshBytes).first(n));});
 bad=meshBytes;put32(bad,16,0xffffffff);reject([&]{decode_mesh(bad);});
 bad=meshBytes;put32(bad,bad.size()-4,99);put32(bad,24,crc32(std::span(bad).subspan(28)));reject([&]{decode_mesh(bad);});
 m.indices[2]=4;reject([&]{encode_mesh(m);});m.indices[2]=2;m.vertices[0]=std::numeric_limits<float>::quiet_NaN();reject([&]{encode_mesh(m);});
 std::cout<<"PASS mesh: roundtrip, every truncation, count overflow, invalid index, nonfinite vertex\n";
 reject([]{content_path("assets","../secret");});reject([]{content_path("assets","/tmp/secret");});reject([]{content_path("assets","C:\\secret");});
 check(content_path("assets","maps/test.cadata").filename()=="test.cadata","valid asset path");
 reject([]{validate_world(Value(Value::Object{{"schema",Value(9.0)}}));});
 std::cout<<"PASS paths/schema: traversal, absolute paths, Windows paths, unsupported schema\n";return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}

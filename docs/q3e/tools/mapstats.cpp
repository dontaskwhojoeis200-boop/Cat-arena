#include "cat/assets.hpp"
#include "cat/physics.hpp"
#include <cstdio>
#include <cmath>
using namespace cat;
static bool convex(const std::vector<std::array<double,2>>& p){
    if(p.size()<3) return false; int sgn=0; size_t n=p.size();
    for(size_t i=0;i<n;++i){auto&a=p[i];auto&b=p[(i+1)%n];auto&c=p[(i+2)%n];
        double cr=(b[0]-a[0])*(c[1]-b[1])-(b[1]-a[1])*(c[0]-b[0]); if(std::fabs(cr)<1e-9) continue; int s=cr>0?1:-1; if(!sgn) sgn=s; else if(s!=sgn) return false;}
    return true; }
int main(int argc,char**argv){
  for(int i=2;i<argc;++i){ auto doc=assets::load_data(std::string(argv[1])+"/maps/"+argv[i]+"/map.cadata");
    physics::CollisionWorld w(doc.at("compiled"));
    size_t cv=0,ramp=0,verts=0,maxv=0; double zmin=1e9,zmax=-1e9;
    for(auto&s:w.slabs()){ cv+=convex(s.poly); ramp+=s.has_plane; verts+=s.poly.size(); maxv=std::max(maxv,s.poly.size()); zmin=std::min(zmin,s.z); zmax=std::max(zmax,s.z);}
    std::printf("%s: slabs %zu (convex %zu, ramps %zu, poly verts %zu, max %zu, z %.2f..%.2f), walls %zu, pads %zu, spawns %zu, pickups %zu\n",argv[i],w.slabs().size(),cv,ramp,verts,maxv,zmin,zmax,w.walls().size(),w.pads().size(),w.spawns().size(),w.pickups().size()); }
}

#include "cat/assets.hpp"
#include <iostream>
int main(int argc,char** argv){
 try{
  if(argc!=2){std::cerr<<"Usage: cat_arena_asset_check <assets/n1>\n";return 2;}
  const std::filesystem::path root(argv[1]);
  for(auto id:{"helios-crucible","bastion-keep","reactor-bastion","q3-gallery"}){
   auto w=cat::assets::load_world(root/"maps"/id/"map.cadata");cat::assets::validate_render_assets(w,root);
   std::cout<<"PASS "<<w.id<<" slabs="<<w.slabs<<" walls="<<w.walls<<" ramps="<<w.ramps<<" spawns="<<w.spawns<<" kits="<<w.kits<<" lightmap="<<(w.lightmap_compatible?"matched":"UNMATCHED")<<'\n';
  }return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}

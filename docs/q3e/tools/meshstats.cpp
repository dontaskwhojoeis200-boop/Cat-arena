#include "cat/assets.hpp"
#include <cstdio>
int main(int c,char**v){ for(int i=1;i<c;++i){ auto m=cat::assets::load_mesh(v[i]); std::printf("%s: %zu vertices (stride %u floats), %zu triangles\n",v[i],m.vertex_count(),m.stride,m.indices.size()/3);} }

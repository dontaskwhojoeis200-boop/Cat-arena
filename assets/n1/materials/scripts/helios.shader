
textures/helios/coolant
{
 cull none
 surfaceparm nolightmap
 {
  map textures/gallery/plasma
  tcMod scale 2 2
  tcMod scroll .035 -.08
  rgbGen const ( .06 .42 .52 )
 }
 {
  map textures/gallery/energy
  blendFunc add
  tcMod scroll -.025 .06
  rgbGen const ( .02 .13 .19 )
 }
}
textures/helios/strip
{
 cull none
 surfaceparm nolightmap
 {
  map $whiteimage
  rgbGen const ( .23 .75 .88 )
 }
}

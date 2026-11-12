
// ---------------------------------------------------------------- lightmapped architecture
textures/gallery/floor_lm
{
	{
		map textures/gallery/floor
	}
	{
		map $lightmap
		blendFunc filter
	}
	{
		map textures/gallery/detail
		blendFunc GL_DST_COLOR GL_SRC_COLOR
		detail
		tcMod scale 5.3 5.3
	}
}
textures/gallery/wall_lm
{
	{
		map textures/gallery/wall
	}
	{
		map $lightmap
		blendFunc filter
	}
	{
		map textures/gallery/detail
		blendFunc GL_DST_COLOR GL_SRC_COLOR
		detail
		tcMod scale 4 4
	}
}
textures/gallery/stone_lm
{
	{
		map textures/gallery/stone
	}
	{
		map $lightmap
		blendFunc filter
	}
}
textures/gallery/metal_lm
{
	{
		map textures/gallery/metal
	}
	{
		map $lightmap
		blendFunc filter
	}
}
// ---------------------------------------------------------------- animated / special
// Animated energy barrier: two additive scrolling layers, pulsing, wobbling.
textures/gallery/energy_barrier
{
	cull none
	surfaceparm trans
	deformVertexes wave 48 sin 0 1.6 0 1.4
	{
		map textures/gallery/energy
		blendFunc add
		tcMod scroll 0 .55
		rgbGen wave sin .65 .35 0 .6
	}
	{
		map textures/gallery/scanlines
		blendFunc add
		tcMod scale 1 3
		tcMod scroll 0 -1.2
		rgbGen const ( .25 .55 .9 )
	}
}
// Scrolling industrial machinery behind a lightmap.
textures/gallery/machinery
{
	{
		map textures/gallery/machinery
		tcMod scroll .18 0
	}
	{
		map $lightmap
		blendFunc filter
	}
}
// Glowing warning panel: lightmapped paint + pulsing additive glow mask.
textures/gallery/warning_panel
{
	{
		map textures/gallery/warning
	}
	{
		map $lightmap
		blendFunc filter
	}
	{
		map textures/gallery/warning_glow
		blendFunc add
		rgbGen wave sin .55 .45 0 .8
	}
}
// animMap: four frame console screen + scrolling scanline overlay.
textures/gallery/console
{
	{
		animMap 3 textures/gallery/console0 textures/gallery/console1 textures/gallery/console2 textures/gallery/console3
	}
	{
		map textures/gallery/scanlines
		blendFunc add
		tcMod scroll 0 .4
		rgbGen const ( .18 .18 .18 )
	}
}
// Transparent glass: alpha-blended dirt layer + additive environment reflection.
textures/gallery/glass
{
	cull none
	sort seeThrough
	{
		map textures/gallery/glass
		blendFunc blend
	}
	{
		map textures/gallery/chrome_env
		tcGen environment
		blendFunc add
		rgbGen const ( .22 .24 .3 )
	}
}
// Reflective metal: base * lightmap + environment map.
textures/gallery/chrome
{
	{
		map textures/gallery/metal
	}
	{
		map $lightmap
		blendFunc filter
	}
	{
		map textures/gallery/chrome_env
		tcGen environment
		blendFunc add
		rgbGen const ( .55 .55 .6 )
	}
}
// Wavy chrome: deformVertexes normal perturbs the reflection.
textures/gallery/chrome_wavy
{
	deformVertexes normal .35 1.2
	{
		map textures/gallery/chrome_env
		tcGen environment
	}
	{
		map $lightmap
		blendFunc GL_DST_COLOR GL_SRC_COLOR
	}
}
// Animated water: vertex waves, turbulent scrolling base, caustics, reflection.
textures/gallery/water
{
	cull none
	sort underwater
	surfaceparm trans
	deformVertexes wave 64 sin 0 2.2 0 .45
	{
		map textures/gallery/water
		blendFunc blend
		tcMod turb 0 .06 0 .18
		tcMod scroll .02 .012
	}
	{
		map textures/gallery/caustics
		blendFunc add
		tcMod scale .6 .6
		tcMod scroll -.035 .05
		rgbGen const ( .35 .48 .55 )
	}
	{
		map textures/gallery/chrome_env
		tcGen environment
		blendFunc add
		rgbGen const ( .12 .16 .22 )
	}
}
// Pulsating reactor: bulge deform + plasma with pulsing rgbGen + cell overlay.
textures/gallery/reactor
{
	deformVertexes bulge 3 4 1.4
	{
		map textures/gallery/plasma
		tcMod scroll 0 .35
		tcMod turb 0 .08 0 .3
		rgbGen wave sin .82 .18 0 1.1
	}
	{
		map textures/gallery/reactor_cells
		blendFunc GL_DST_COLOR GL_SRC_COLOR
	}
	{
		map textures/gallery/plasma
		blendFunc add
		tcMod scale 2 2
		tcMod scroll .1 -.6
		rgbGen wave triangle .25 .25 .5 2
	}
}
// Moving conveyor belt.
textures/gallery/conveyor
{
	{
		map textures/gallery/conveyor
		tcMod scroll 0 -.45
	}
	{
		map $lightmap
		blendFunc filter
	}
}
// Alpha-tested grate (holes are see-through), lightmap on the solid parts only.
textures/gallery/grate
{
	cull none
	surfaceparm alphashadow
	{
		map textures/gallery/grate
		alphaFunc GE128
		depthWrite
	}
	{
		map $lightmap
		blendFunc filter
		depthFunc equal
	}
}
// Rotating fan: tcMod rotate + alpha test.
textures/gallery/fan
{
	cull none
	{
		map textures/gallery/fan
		alphaFunc GE128
		depthWrite
		tcMod rotate 220
	}
	{
		map $lightmap
		blendFunc filter
		depthFunc equal
	}
}
// Waving banner (deformVertexes wave) with alpha-tested tatters.
textures/gallery/banner
{
	cull none
	deformVertexes wave 30 sin 0 3 0 .7
	{
		map textures/gallery/banner
		alphaFunc GE128
		depthWrite
	}
	{
		map $lightmap
		blendFunc filter
		depthFunc equal
	}
}
// Moving piston (deformVertexes move) - mechanical motion without new geometry.
textures/gallery/piston
{
	deformVertexes move 0 0 18 sin 0 1 0 .35
	{
		map textures/gallery/metal
	}
	{
		map $lightmap
		blendFunc filter
	}
}
// tcMod stretch + rotate + transform + tcGen vector demonstrations.
textures/gallery/stretch_light
{
	{
		map textures/gallery/swirl
		tcMod stretch sin 1 .25 0 .4
		tcMod rotate 45
		rgbGen identity
	}
}
textures/gallery/vector_panel
{
	{
		map textures/gallery/reactor_cells
		tcGen vector ( .0078 0 0 ) ( 0 0 .0078 )
		tcMod transform 1 .25 0 1 0 0
	}
	{
		map $lightmap
		blendFunc filter
	}
}
// Autosprite flares and autosprite2 light shafts.
textures/gallery/flare
{
	deformVertexes autosprite
	sort additive
	{
		map textures/gallery/flare
		blendFunc add
		rgbGen wave noise .8 .2 0 3
	}
}
textures/gallery/beam
{
	deformVertexes autosprite2
	cull none
	sort additive
	{
		map textures/gallery/flare
		blendFunc add
		tcMod scale 1 .5
		rgbGen const ( .45 .32 .18 )
	}
}
// Vertex-coloured light strip (rgbGen vertex) and alphaGen wave blinker.
textures/gallery/lightstrip
{
	{
		map $whiteimage
		rgbGen vertex
	}
}
textures/gallery/blinker
{
	sort additive
	{
		map textures/gallery/flare
		blendFunc blend
		rgbGen const ( 1 .25 .1 )
		alphaGen wave square .5 .5 0 1
	}
}
// ---------------------------------------------------------------- mirrors and portals
textures/gallery/mirror
{
	mirror
	{
		map $mirror
	}
	{
		map textures/gallery/glass
		blendFunc GL_ONE GL_ONE
		rgbGen const ( .08 .08 .09 )
	}
}
textures/gallery/portal
{
	portal
	{
		map $portal
	}
	{
		map textures/gallery/swirl
		blendFunc blend
		tcMod rotate 60
		alphaGen portal 768
	}
}
textures/gallery/monitor
{
	portal
	{
		map $portalfeed
	}
	{
		map textures/gallery/scanlines
		blendFunc add
		tcMod scroll 0 .3
		rgbGen const ( .12 .12 .12 )
	}
}
// ---------------------------------------------------------------- models (MD3)
models/gallery/turret
{
	{
		map textures/gallery/metal
		rgbGen lightingDiffuse
	}
	{
		map textures/gallery/warning_glow
		blendFunc add
		rgbGen entity
	}
}
models/gallery/barrel
{
	{
		map textures/gallery/chrome_env
		tcGen environment
	}
	{
		map textures/gallery/metal
		blendFunc filter
		rgbGen lightingDiffuse
	}
}
models/gallery/flag
{
	cull none
	{
		map textures/gallery/banner
		alphaFunc GE128
		rgbGen lightingDiffuse
	}
}
// ---------------------------------------------------------------- skies
sky/gallery
{
	skyParms env/gallery_nebula 192 -
	skyRotate .6
	{
		map sky/gallery_clouds1
		blendFunc blend
		tcMod scale 1.2 1.2
		tcMod scroll .006 .003
	}
	{
		map sky/gallery_clouds2
		blendFunc blend
		tcMod scale 2 2
		tcMod scroll -.004 .006
	}
}
// Reactor Bastion's authored sunset sky as a skybox baked at load from its
// sky geometry, plus its two cloud layers (vertex colour = sun warmth / horizon fade).
sky/reactor_bastion
{
	skyParms $baked 160 -
	{
		map sky/rb_clouds_a
		blendFunc blend
		tcMod scale .55 .55
		tcMod scroll .0045 .0018
		rgbGen vertex
		alphaGen vertex
	}
	{
		map sky/rb_clouds_b
		blendFunc blend
		tcMod scale .9 .9
		tcMod scroll .009 -.003
		rgbGen vertex
		alphaGen vertex
	}
}


textures/base/curve_wall
{
	{
		map $world:1
		rgbGen vertex
	}
	{
		map $lightmap
		blendFunc filter
	}
}
textures/base/curve_floor
{
	{
		map $world:2
		rgbGen vertex
	}
	{
		map $lightmap
		blendFunc filter
	}
}
// two-sided curved floor (helical ramp seen from below as well)
textures/base/curve_floor_2s
{
	cull none
	{
		map $world:2
		rgbGen vertex
	}
	{
		map $lightmap
		blendFunc filter
	}
}
textures/base/curve_ceiling
{
	{
		map $world:3
		rgbGen vertex
	}
	{
		map $lightmap
		blendFunc filter
	}
}
textures/base/curve_trim
{
	{
		map $world:6
		rgbGen vertex
	}
	{
		map $lightmap
		blendFunc filter
	}
}
textures/base/curve_rust
{
	{
		map $world:9
		rgbGen vertex
	}
	{
		map $lightmap
		blendFunc filter
	}
}
textures/base/curve_grate
{
	cull none
	{
		map $world:14
		rgbGen vertex
		alphaFunc GE128
		depthWrite
	}
	{
		map $lightmap
		blendFunc filter
		depthFunc equal
	}
}

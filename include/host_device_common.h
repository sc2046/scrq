// Common file shared across C++ CPU code and GLSL GPU code.
#ifndef HOST_DEVICE_COMMON_H
#define HOST_DEVICE_COMMON_H

#ifdef __cplusplus
#include <cstdint>
#include <glm.hpp>
using uint = uint32_t;
using vec2 = glm::vec2;
using uvec2 = glm::uvec2;
using uvec3 = glm::uvec3;
using vec3 = glm::vec3;
#endif  // #ifdef __cplusplus



// TODO: (Hack) fix number of meshes a scene can contain.
#define MAX_MESH_COUNT 64 
#define SPHERE_CUSTOM_INDEX MAX_MESH_COUNT 
#define MAX_TEXTURE_COUNT 500

// Materials
#define DIFFUSE		0
#define MIRROR		1
#define DIELECTRIC	2
#define PHONG		3
#define LIGHT		4

// Integrators
#define PATH		0
#define NORMAL		1
#define AO			2
// Samplers




// ==============================================================
// Constants
// ==============================================================
#define M_PI         3.14159265358979323846f
#define INV_PI       0.31830988618379067154f
#define INV_TWOPI    0.15915494309189533577f
#define INV_FOURPI   0.07957747154594766788f
#define SQRT_TWO     1.41421356237309504880f
#define INV_SQRT_TWO 0.70710678118654752440f

struct Vertex
{
	vec3 position;
	vec3 normal;
	vec2 tex;
};

struct AABB
{
	vec3 min;
	vec3 max;
};

// Right-handed pinhole camera with (0,1,0) as the world up vector.
// Note a non-black background is equivalent to treating the background as a light source.
struct Camera
{
	vec3	center;
	vec3	eye;
	vec3	backgroundColor;
	float	fovY;
	float	focalDistance;
};

struct Material
{
	uint	type;
	vec3	albedo;
	//uint    albdeoTextureID;
	int		phongExponent;	// Only used if type == PHONG
	vec3	emitted;	// Only used if type == LIGHT
	//float	ior;		// Only used if type == DIELECTRIC
};

#endif // #ifndef COMMON_H
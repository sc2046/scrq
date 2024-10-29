#version 460
#extension GL_EXT_ray_query : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_GOOGLE_include_directive : require
#include "host_device_common.h"
#include "device_common.glsl"


layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(binding = 0, set = 0, scalar) buffer storageBuffer { vec3 imageData[]; };
layout(binding = 1, set = 0) uniform accelerationStructureEXT tlas;
layout(binding = 2, set = 0, scalar) buffer Spheres { Sphere spheres[]; };

// The resolution of the buffer, which in this case is a hardcoded vector of 2 unsigned integers:
const uvec2 resolution = uvec2(1700, 900);

vec3 rayColor(Ray ray);
vec3 skyColor(vec3 direction);

void main()
{
	const uvec2 pixel = gl_GlobalInvocationID.xy;
	if ((pixel.x >= resolution.x) || (pixel.y >= resolution.y)) { return; }

	// Define the camera paremeters.
	Camera camera;
	camera.origin	= vec3(0.f, 0.f, 0.f);
	camera.fovY		= 90.f;

	Ray ray = generateRay(camera, vec2(pixel), resolution);

	vec3 pixelColor = rayColor(ray);

	const uint pixelStart = resolution.x * pixel.y + pixel.x;
	imageData[pixelStart] = pixelColor;
}


vec3 rayColor(Ray ray)
{
	// Initialise a ray query object.
	rayQueryEXT rayQuery;
	const float tMin = 0.f;
	const float tMax = 10000.f;
	rayQueryInitializeEXT(rayQuery, tlas, gl_RayFlagsOpaqueEXT, 0xFF, ray.origin, tMin, ray.direction, tMax);

	// Traverse scene, keeping track of the ray parameter at the closest intersection.
	float tClosest = tMax;
	while (rayQueryProceedEXT(rayQuery))
	{
		// Check intersections with each (leaf) AABB.
		if (rayQueryGetIntersectionTypeEXT(rayQuery, false) == gl_RayQueryCandidateIntersectionAABBEXT)
		{
			const int sphereID	= rayQueryGetIntersectionPrimitiveIndexEXT(rayQuery, false);
			const Sphere sphere = spheres[sphereID];
			const float tHit	= hitSphere(sphere, ray); 
			
			// Update closest intersection.
			if (tHit > tMin && tHit < tClosest)
			{
				tClosest = tHit;
				rayQueryGenerateIntersectionEXT(rayQuery, tHit);
			}
		}
	}

	// No intersections, return background color.
	if(rayQueryGetIntersectionTypeEXT(rayQuery, true) == gl_RayQueryCommittedIntersectionNoneEXT) {
		return skyColor(ray.direction);
	}
	// Ignore triangles for now.
	else if (rayQueryGetIntersectionTypeEXT(rayQuery, true) == gl_RayQueryCommittedIntersectionTriangleEXT) {
		return vec3(0.f);
	}
	else {
		const int sphereID  = rayQueryGetIntersectionPrimitiveIndexEXT(rayQuery, true);
		const Sphere sphere = spheres[sphereID];
		//const float tHit  = hitSphere(sphere, ray); // We generated the correct t-value inside the scene traversal loop.
		const float tHit	= rayQueryGetIntersectionTEXT(rayQuery,true); 
		const vec3 hitPoint = ray.origin + tHit * ray.direction;
		const vec3 N		= normalize(hitPoint - sphere.center);
		return  0.5 * vec3(N.x + 1, N.y + 1, N.z + 1);
	}
}


vec3 skyColor(vec3 direction)
{
	if (direction.y > 0.0f) {
		return mix(vec3(1.0f), vec3(0.25f, 0.5f, 1.0f), direction.y);
	}
	return vec3(0.03f);
	
}


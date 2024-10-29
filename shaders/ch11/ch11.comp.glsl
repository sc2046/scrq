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



const uvec2 resolution = uvec2(1700, 900);
const uint numSamples = 100;
const uint numBounces = 3;



vec3 rayColor(Ray ray, inout uint rngState);
vec3 skyColor(vec3 direction);
void main()
{
	const uvec2 pixel = gl_GlobalInvocationID.xy;
	if ((pixel.x >= resolution.x) || (pixel.y >= resolution.y)) { return; }

	// Use the linear index of the pixel as the initial seed for the RNG.
	uint rngState = resolution.x * pixel.y + pixel.x;

	// Define the camera paremeters.
	Camera camera;
	camera.origin	= vec3(0.f, 0.f, 0.f);
	camera.fovY		= 90.f;

	vec3 pixelColor = vec3(0.f);
	for (int sampleID = 0; sampleID < numSamples; ++sampleID)
	{
		Ray ray = generateRay(camera, vec2(pixel) + vec2(stepAndOutputRNGFloat(rngState), stepAndOutputRNGFloat(rngState)), resolution);
		pixelColor += rayColor(ray, rngState);
	}
	pixelColor /= numSamples;
	const uint pixelStart = resolution.x * pixel.y + pixel.x;
	imageData[pixelStart] = pixelColor;
}


vec3 rayColor(Ray ray, inout uint rngState)
{
	vec3 curAttenuation = vec3(1.0);

	for (int depth = 0; depth < numBounces; ++depth)
	{
		// Initialise a ray query object.
		rayQueryEXT rayQuery;
		const float tMin = 0.001f;
		const float tMax = 10000.f;
		rayQueryInitializeEXT(rayQuery, tlas, gl_RayFlagsOpaqueEXT, 0xFF, ray.origin, tMin, ray.direction, tMax);

		// Traverse scene, keeping track of the information at the closest intersection.
		HitInfo hitInfo;
		hitInfo.t = tMax;
		while (rayQueryProceedEXT(rayQuery))
		{
			// Check intersections with each (leaf) AABB.
			if (rayQueryGetIntersectionTypeEXT(rayQuery, false) == gl_RayQueryCandidateIntersectionAABBEXT)
			{
				int sphereID	= rayQueryGetIntersectionPrimitiveIndexEXT(rayQuery, false);
				Sphere sphere	= spheres[sphereID];
				float tHit		= hitSphere(sphere, ray);

				// Update closest intersection.
				if (tHit > tMin && tHit < hitInfo.t)
				{
					// TODO: For now, world space = local space.
					hitInfo.t		= tHit;
					hitInfo.p		= ray.origin + tHit * ray.direction;
					hitInfo.gn		= normalize(hitInfo.p - sphere.center);
					hitInfo.sn		= hitInfo.gn;
					hitInfo.color	= vec3(0.5f);

					rayQueryGenerateIntersectionEXT(rayQuery, tHit);
				}
			}
		}
	
		if (rayQueryGetIntersectionTypeEXT(rayQuery, true) == gl_RayQueryCommittedIntersectionNoneEXT) {
			// No geometry intesections occured. Note that we treat the background as if it is a light source.
			return curAttenuation * skyColor(ray.direction);
		}
		else if (rayQueryGetIntersectionTypeEXT(rayQuery, true) == gl_RayQueryCommittedIntersectionTriangleEXT) {
			// Ignore triangles for now.
			return vec3(0.f);
		}
		else {
			vec3 attenuation;
			Ray scattered;
			const int primitiveID = rayQueryGetIntersectionPrimitiveIndexEXT(rayQuery, true);
			
			const bool scatter = primitiveID == 2 ?
				scatterDielectric(ray, hitInfo, attenuation, scattered, rngState) :
				scatterDiffuse(ray, hitInfo, attenuation, scattered, rngState);
			if (scatter) {
				curAttenuation *= attenuation;
				ray = scattered;
			}
			else return vec3(0.f);
		}
	}
	// Exceeded recursion - assume the sample provides no contribution to the light.
	return vec3(0.f);
}

vec3 skyColor(vec3 direction)
{
	const vec3 unit_direction = normalize(direction);
	float a = 0.5 * (unit_direction.y + 1.0);
	return (1.0 - a) * vec3(1.0, 1.0, 1.0) + a * vec3(0.5, 0.7, 1.0);
}


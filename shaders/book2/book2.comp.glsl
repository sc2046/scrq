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
layout(binding = 3, set = 0, scalar) buffer Vertices { Vertex vertices[]; };
layout(binding = 4, set = 0, scalar) buffer Indices { uint indices[]; };

layout(push_constant, scalar) uniform PushConstants
{
	Camera camera;
	uint numSamples;
	uint numBounces;
};

const uvec2 resolution = uvec2(800, 600);

vec3 rayColor(vec3 origin, vec3 direction, inout uint rngState);

void main()
{

	const uvec2 pixel = gl_GlobalInvocationID.xy;
	if ((pixel.x >= resolution.x) || (pixel.y >= resolution.y)) { return; }

	// Use the linear index of the pixel as the initial seed for the RNG.
	uint rngState = resolution.x * pixel.y + pixel.x;

	vec3 pixelColor = vec3(0.f);
	for (int sampleID = 0; sampleID < numSamples; ++sampleID)
	{
		vec3 origin;
		vec3 direction;
		generateRay(camera, vec2(pixel) + vec2(stepAndOutputRNGFloat(rngState), stepAndOutputRNGFloat(rngState)), resolution, origin, direction);
		pixelColor += rayColor(origin, direction, rngState);
	}
	pixelColor /= numSamples;
	const uint pixelStart = resolution.x * pixel.y + pixel.x;
	imageData[pixelStart] = pixelColor;
}

vec3 rayColor(vec3 origin, vec3 direction, inout uint rngState)
{
	vec3 curAttenuation = vec3(1.0);

	for (int depth = 0; depth < numBounces; ++depth)
	{
		// Initialise a ray query object.
		rayQueryEXT rayQuery;
		const float tMin = 0.f;
		const float tMax = 10000.f;
		rayQueryInitializeEXT(rayQuery, tlas, gl_RayFlagsOpaqueEXT, 0xFF, origin, tMin, direction, tMax);

		// Traverse scene, keeping track of the information at the closest intersection.
		HitInfo hitInfo;
		hitInfo.t = tMax;
		while (rayQueryProceedEXT(rayQuery))
		{
			// For procedural geometry (i.e. geometry defined by AABBs), we must handle intersection routines ourselves.
			if (rayQueryGetIntersectionTypeEXT(rayQuery, false) == gl_RayQueryCandidateIntersectionAABBEXT)
			{
				const int geometryID	= rayQueryGetIntersectionInstanceCustomIndexEXT(rayQuery, false);
				const int materialID	= int(rayQueryGetIntersectionInstanceShaderBindingTableRecordOffsetEXT(rayQuery, false));
				
				const Sphere sphere		= spheres[geometryID];

				// TODO: perform intersection tests in object space so we dont need to store sphere buffer.
				//const mat4x3 objectToWorld = rayQueryGetIntersectionObjectToWorldEXT(rayQuery, false);
				//const mat4x3 worldToObject = rayQueryGetIntersectionWorldToObjectEXT(rayQuery, false);

				//if (hitSphere(origin, direction, worldToObject, objectToWorld, hitInfo)) {
				//	hitInfo.material = materialID;
				//	rayQueryGenerateIntersectionEXT(rayQuery, hitInfo.t);
				//}

				if (hitSphere(sphere, origin, direction, hitInfo)) {
					hitInfo.material = materialID; 
					rayQueryGenerateIntersectionEXT(rayQuery, hitInfo.t);
				}
			}
		}

		if (rayQueryGetIntersectionTypeEXT(rayQuery, true) == gl_RayQueryCommittedIntersectionNoneEXT) {
			return curAttenuation * camera.backgroundColor;
		}
		// Fill intersection data for triangles.
		else if (rayQueryGetIntersectionTypeEXT(rayQuery, true) == gl_RayQueryCommittedIntersectionTriangleEXT) {
			
			// Get the ID of the triangle
			const int triangleID = rayQueryGetIntersectionPrimitiveIndexEXT(rayQuery, true);

			// Get the indices of the vertices of the triangle
			const uint i0 = indices[3 * triangleID + 0];
			const uint i1 = indices[3 * triangleID + 1];
			const uint i2 = indices[3 * triangleID + 2];

			// Get the vertices of the triangle
			const Vertex v0 = vertices[i0];
			const Vertex v1 = vertices[i1];
			const Vertex v2 = vertices[i2];

			// Get the barycentric coordinates of the intersection
			vec3 barycentrics = vec3(0.f, rayQueryGetIntersectionBarycentricsEXT(rayQuery, true));
			barycentrics.x = 1.f - barycentrics.y - barycentrics.z;

			// Compute the coordinates of the intersection
			const vec3 objectPos	= v0.position * barycentrics.x + v1.position * barycentrics.y + v2.position * barycentrics.z;
			const vec3 objectSN		= v0.normal * barycentrics.x + v1.normal * barycentrics.y + v2.normal * barycentrics.z;
			const vec3 objectGN		= normalize(cross(v1.position - v0.position, v2.position - v0.position));
			const vec2 objectUV		= v0.tex * barycentrics.x + v1.tex * barycentrics.y + v2.tex * barycentrics.z;


			hitInfo.t	= rayQueryGetIntersectionTEXT(rayQuery, true);
			hitInfo.p	= rayQueryGetIntersectionObjectToWorldEXT(rayQuery, true) * vec4(objectPos, 1.0f);
			hitInfo.gn	= normalize((objectGN * rayQueryGetIntersectionWorldToObjectEXT(rayQuery, true)).xyz);
			hitInfo.sn	= objectSN; //TODO: Not sure about this...
			//hitInfo.uv	= objectUV;
			//hitInfo.material = int(rayQueryGetIntersectionInstanceShaderBindingTableRecordOffsetEXT(rayQuery, true)); //TODO: Requires materials.
			
			return vec3(0.5) + 0.5 * hitInfo.gn;

			hitInfo.material = DIFFUSE;
			hitInfo.color = vec3(0.5f);
		}
		// Fill intersection data for AABBs.
		else {

		}

		// Now use material to determine scatter properties.
		vec3 attenuation;
		vec3 scatteredOrigin;
		vec3 scatteredDir;

		bool scatter;
		vec3 emitted = vec3(0.f);
		switch (hitInfo.material) {
		case DIFFUSE:
			scatter = scatterDiffuse(origin, direction, hitInfo, attenuation, scatteredOrigin, scatteredDir, rngState);
			break;
		case METAL:
			scatter = scatterMetal(origin, direction, hitInfo, attenuation, scatteredOrigin, scatteredDir, rngState);
			break;
		case DIELECTRIC:
			scatter = scatterDielectric(origin, direction, hitInfo, attenuation, scatteredOrigin, scatteredDir, rngState);
			break;
		case LIGHT:
			scatter = false;
			emitted = vec3(4.f);
		default:
			scatter = false;
			break;
		}
		if (scatter) {
			curAttenuation	*= attenuation;
			curAttenuation	+= emitted;
			origin 			= scatteredOrigin;
			direction		= scatteredDir;
		}
		else return emitted;
	}
	// Exceeded recursion - assume the sample provides no contribution to the light.
	return vec3(0.f);
}


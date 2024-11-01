#version 460
#extension GL_EXT_ray_query : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_GOOGLE_include_directive : require

#include "host_device_common.h"
#include "device_common.glsl"

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(binding = 0, set = 0, rgba32f) uniform image2D storageImage;
layout(binding = 1, set = 0) uniform accelerationStructureEXT tlas;
layout(binding = 2, set = 0, scalar) buffer Vertices { Vertex vertices[]; } meshVertices[MAX_MESH_COUNT];	// Contains vertex buffers for meshes in the scene
layout(binding = 3, set = 0, scalar) buffer Indices { uint indices[]; }		meshIndices[MAX_MESH_COUNT];	// Contains index buffers of meshes in the scene.
layout(binding = 4, set = 0, scalar) buffer Materials { Material materials[]; };							// Contains all materials for the scene


layout(push_constant, scalar) uniform PushConstants
{
	Camera camera;
	uint numSamples;
	uint numBounces;
};

vec3 rayColor(vec3 origin, vec3 direction, inout uint rngState);

void main()
{
	// The resolution of the image:
	const ivec2 resolution = imageSize(storageImage);

	const uvec2 pixel = gl_GlobalInvocationID.xy;
	if ((pixel.x >= resolution.x) || (pixel.y >= resolution.y)) { return; }

	// Use the linear index of the pixel as the initial seed for the RNG.
	uint rngState = uint(resolution.x * pixel.y + pixel.x);

	vec3 pixelColor = vec3(0.f);
	for (int sampleID = 0; sampleID < numSamples; ++sampleID)
	{
		vec3 origin;
		vec3 direction;
		generateRay(camera, vec2(pixel) + vec2(stepAndOutputRNGFloat(rngState), stepAndOutputRNGFloat(rngState)), resolution, origin, direction);
		pixelColor += rayColor(origin, direction, rngState);
	}
	pixelColor /= numSamples;
	imageStore(storageImage, ivec2(pixel), vec4(pixelColor, 0.f));
}

vec3 rayColor(vec3 origin, vec3 direction, inout uint rngState)
{
	vec3 curAttenuation = vec3(1.0);
	vec3 result			= vec3(0.f);
	for (int depth = 0; depth <= numBounces; ++depth)
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
				// For procedural geometry, we use the custom index to determine the type of the geometry.
				const uint geometryType	= rayQueryGetIntersectionInstanceCustomIndexEXT(rayQuery, false);
				const uint materialID	= rayQueryGetIntersectionInstanceShaderBindingTableRecordOffsetEXT(rayQuery, false);

				// TODO: perform intersection tests in object space to simplify intersecion routines.
				// (FOr spheres it doesnt really matter but it might help if you want to add other types of procedural geometry.)
				const mat4x3 objectToWorld = rayQueryGetIntersectionObjectToWorldEXT(rayQuery, false);
				const mat4x3 worldToObject = rayQueryGetIntersectionWorldToObjectEXT(rayQuery, false);
				
				const vec3 localO = rayQueryGetIntersectionObjectRayOriginEXT(rayQuery, false);
				const vec3 localD = rayQueryGetIntersectionObjectRayDirectionEXT(rayQuery, false);

				if ( geometryType == SPHERE_CUSTOM_INDEX && hitSphere(localO, localD,  worldToObject, objectToWorld, hitInfo)) {
					Material material = materials[materialID];
						
					hitInfo.materialType = material.type;
					hitInfo.albedo = material.albedo;
					rayQueryGenerateIntersectionEXT(rayQuery, hitInfo.t);
				}

				// Other procedural geometry...
			}
		}

		// Determine hit info at closest hit
		if (rayQueryGetIntersectionTypeEXT(rayQuery, true) == gl_RayQueryCommittedIntersectionNoneEXT) {
			return curAttenuation * camera.backgroundColor;
		}
		else if (rayQueryGetIntersectionTypeEXT(rayQuery, true) == gl_RayQueryCommittedIntersectionTriangleEXT) {
			
			// Get the ID of the triangle
			const uint meshID		= rayQueryGetIntersectionInstanceCustomIndexEXT(rayQuery, true);
			const uint triangleID	= rayQueryGetIntersectionPrimitiveIndexEXT(rayQuery, true);
			const uint materialID	= rayQueryGetIntersectionInstanceShaderBindingTableRecordOffsetEXT(rayQuery, true);

			// Get the indices of the vertices of the triangle
			const uint i0 = meshIndices[meshID].indices[3 * triangleID + 0];
			const uint i1 = meshIndices[meshID].indices[3 * triangleID + 1];
			const uint i2 = meshIndices[meshID].indices[3 * triangleID + 2];

			// Get the vertices of the triangle
			const Vertex v0 = meshVertices[meshID].vertices[i0];
			const Vertex v1 = meshVertices[meshID].vertices[i1];
			const Vertex v2 = meshVertices[meshID].vertices[i2];

			// Get the barycentric coordinates of the intersection
			vec3 barycentrics	= vec3(0.f, rayQueryGetIntersectionBarycentricsEXT(rayQuery, true));
			barycentrics.x		= 1.f - barycentrics.y - barycentrics.z;

			// Compute the coordinates of the intersection
			const vec3 objectPos	= v0.position * barycentrics.x + v1.position * barycentrics.y + v2.position * barycentrics.z;
			const vec3 objectSN		= v0.normal * barycentrics.x + v1.normal * barycentrics.y + v2.normal * barycentrics.z;
			const vec3 objectGN		= normalize(cross(v1.position - v0.position, v2.position - v0.position));
			const vec2 objectUV		= v0.tex * barycentrics.x + v1.tex * barycentrics.y + v2.tex * barycentrics.z;


			hitInfo.t	= rayQueryGetIntersectionTEXT(rayQuery, true);
			hitInfo.p	= rayQueryGetIntersectionObjectToWorldEXT(rayQuery, true) * vec4(objectPos, 1.0f);
			hitInfo.gn	= normalize((objectGN * rayQueryGetIntersectionWorldToObjectEXT(rayQuery, true)).xyz);
			hitInfo.sn  = normalize((objectSN * rayQueryGetIntersectionWorldToObjectEXT(rayQuery, true)).xyz);
			//hitInfo.uv	= objectUV;
			Material material		= materials[materialID];

			hitInfo.materialType	= material.type;
			hitInfo.albedo			= material.albedo;

		}
		else {
			// We already computed hit info for procedural geometry in the traversal loop.
		}


		// Now use material to determine scatter properties.
		vec3 attenuation;
		vec3 scatteredOrigin;
		vec3 scatteredDir;

		bool scatter;
		vec3 emitted = vec3(0.f);
		switch (hitInfo.materialType) {
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
			emitted = hitInfo.albedo; // Assume the albedo represents emission for lights.
			break;
		default:
			scatter = false;
			break;
		}
		if (scatter) {
			result += (curAttenuation * emitted);
			curAttenuation *= attenuation;
			origin 			= scatteredOrigin;
			direction		= scatteredDir;
		}
		else {
			result += (curAttenuation * emitted);
			return result;
		}
	}
	// Exceeded recursion - assume the sample provides no contribution to the light.
	return vec3(0.f);
}


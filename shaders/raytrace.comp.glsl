#version 460
#extension GL_EXT_ray_query : require
#extension GL_EXT_scalar_block_layout : require

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(binding = 0, set = 0, scalar) buffer storageBuffer {	vec3 imageData[];};

layout(binding = 1, set = 0) uniform accelerationStructureEXT triangleTlas;
layout(binding = 2, set = 0, scalar) buffer Vertices { vec3 vertices[]; };
layout(binding = 3, set = 0, scalar) buffer Indices  { uint indices[]; };

layout(binding = 4, set = 0) uniform accelerationStructureEXT sphereTlas;
struct AABB
{
	vec3 min;
	vec3 max;
};
layout(binding = 5, set = 0, scalar) buffer AABBs { AABB aabbs[]; };



// Random number generation using pcg32i_random_t, using inc = 1. Our random state is a uint.
uint stepRNG(uint rngState)
{
	return rngState * 747796405 + 1;
}

// Steps the RNG and returns a floating-point value between 0 and 1 inclusive.
float stepAndOutputRNGFloat(inout uint rngState)
{
	// Condensed version of pcg_output_rxs_m_xs_32_32, with simple conversion to floating-point [0,1].
	rngState = stepRNG(rngState);
	uint word = ((rngState >> ((rngState >> 28) + 4)) ^ rngState) * 277803737;
	word = (word >> 22) ^ word;
	return float(word) / 4294967295.0f;
}


// The resolution of the buffer, which in this case is a hardcoded vector of 2 unsigned integers:
const uvec2 resolution = uvec2(1700, 900);
const uint numSamples = 1000;
const uint numBounces = 8;

struct Camera 
{
	vec3	origin;
	float	fovVerticalSlope; 	// Define the field of view by the vertical slope of the topmost rays:
};

struct Ray
{
	vec3 origin;
	vec3 direction;
};

struct HitInfo
{
	vec3 color;
	vec3 worldPosition;
	vec3 worldNormal;
};

Ray generateRay(Camera cam, vec2 pixel);
HitInfo getObjectHitInfo(rayQueryEXT rayQuery);
vec3 rayColor(Ray ray, inout uint rngState);
vec3 skyColor(vec3 direction);

void main()
{
	const uvec2 pixel = gl_GlobalInvocationID.xy;

	// If the pixel is outside of the image, don't do anything:
	if ((pixel.x >= resolution.x) || (pixel.y >= resolution.y))	{return;}

	// Use the linear index of the pixel as the initial seed for the RNG.
	uint rngState = resolution.x * pixel.y + pixel.x;  

	// Define the camera.
	Camera camera;
	camera.origin = vec3(0.f, 0.f, 0.f);
	//camera.fovVerticalSlope = 1.0 / 5.0;

	// The sum of the colors of all of the samples.
	vec3 pixelColor = vec3(0.f);

	//for (int sampleID = 0; sampleID < numSamples; ++sampleID)
	//{
		// Generate a ray for this sample
		Ray ray = generateRay(camera, vec2(pixel) + vec2(stepAndOutputRNGFloat(rngState), stepAndOutputRNGFloat(rngState)));
		
		//Initialize a ray query object:
		rayQueryEXT rayQuery;
		rayQueryInitializeEXT(
			rayQuery, sphereTlas,
			gl_RayFlagsTerminateOnFirstHitEXT,
			0xFF, ray.origin, 0.f, ray.direction, 10000.f);

		while (rayQueryProceedEXT(rayQuery))
		{
			if (rayQueryGetIntersectionTypeEXT(rayQuery, false) == gl_RayQueryCandidateIntersectionAABBEXT)
			{
				const vec3 center = vec3(0.f,0.f,-1.f);
				const float radius = 0.5f;
				
				const vec3 oc = center - ray.origin;
				float a = dot(ray.direction, ray.direction);
				float b = -2.0 * dot(ray.direction, oc);
				float c = dot(oc, oc) - radius * radius;
				float discriminant = b * b - 4 * a * c;
				if (discriminant >= 0) {
					pixelColor = vec3(1.f, 0.f, 0.f);
				}

				//if (opaqueHit) rayQueryGenerateIntersectionEXT(rayQuery, ...);
			}
		}
		// Add contribution from ray path.
		//pixelColor += rayColor(ray, rngState);
	//}

	//pixelColor /= numSamples;
	const uint pixelStart = resolution.x * pixel.y + pixel.x;
	imageData[pixelStart] = pixelColor;
}

Ray generateRay(Camera cam, vec2 pixel)
{
	Ray ray;

	float uFovy = 90.f;
	ray.origin = cam.origin;
	const vec2 xy = (pixel - resolution / 2.f) / resolution.y;
	const float z = 1.f / (2.f * tan(radians(uFovy) / 2.f));
	ray.direction = normalize(vec3(xy.x, -xy.y, -z));

	//const vec2 screenUV = vec2(
	//	 (2.0 * pixel.x - resolution.x) / resolution.y,    
	//	-(2.0 * pixel.y - resolution.y) / resolution.y);  // Flip the y axis
	//
	//ray.origin		= cam.origin;
	//ray.direction	= normalize(vec3(cam.fovVerticalSlope * screenUV.x, cam.fovVerticalSlope * screenUV.y, -1.0));
	return ray;
}

HitInfo getObjectHitInfo(rayQueryEXT rayQuery)
{
	HitInfo result;
	// Get the ID of the triangle
	const int primitiveID = rayQueryGetIntersectionPrimitiveIndexEXT(rayQuery, true);

	// Get the indices of the vertices of the triangle
	const uint i0 = indices[3 * primitiveID + 0];
	const uint i1 = indices[3 * primitiveID + 1];
	const uint i2 = indices[3 * primitiveID + 2];

	// Get the vertices of the triangle
	const vec3 v0 = vertices[i0];
	const vec3 v1 = vertices[i1];
	const vec3 v2 = vertices[i2];

	// Get the barycentric coordinates of the intersection
	vec3 barycentrics	= vec3(0.f, rayQueryGetIntersectionBarycentricsEXT(rayQuery, true));
	barycentrics.x		= 1.f - barycentrics.y - barycentrics.z;

	// Compute the coordinates of the intersection
	const vec3 objectPos = v0 * barycentrics.x + v1 * barycentrics.y + v2 * barycentrics.z;
	result.worldPosition = /*rayQueryGetIntersectionObjectToWorldEXT(ray, true) **/ objectPos;

	// Get normal using right hand-rule.
	const vec3 objectNormal = normalize(cross(v1 - v0, v2 - v0));
	result.worldNormal = objectNormal; 	//TODO: Check correct local-world transform for normals. 

	result.color = vec3(0.8f);
	const float dotX = dot(result.worldNormal, vec3(1.0, 0.0, 0.0));
	if (dotX > 0.99) { result.color = vec3(0.8, 0.0, 0.0); }
	else if (dotX < -0.99) { result.color = vec3(0.0, 0.8, 0.0);}
	
	return result;
}

vec3 rayColor(Ray ray, inout uint rngState)
{
	// Note that in the iterative version, 
	vec3 curAttenuation = vec3(1.0);

	// Note that 
	for (int depth = 0; depth < numBounces; ++depth)
	{
		//Initialize a ray query object:
		rayQueryEXT rayQuery;
		rayQueryInitializeEXT(
			rayQuery, triangleTlas,
			gl_RayFlagsOpaqueEXT,
			0xFF, ray.origin, 0.f, ray.direction, 10000.f);

		// Traverse the scene. For now, we won't care about any intersections except the commited (closest) one.
		while (rayQueryProceedEXT(rayQuery))
		{}

		// No intersection was found for the scene - return background colors
		if (rayQueryGetIntersectionTypeEXT(rayQuery, true) == gl_RayQueryCommittedIntersectionNoneEXT)
		{
			return curAttenuation * skyColor(ray.direction);
		}

		// If the closest intersection was with a triangle:
		else if (rayQueryGetIntersectionTypeEXT(rayQuery, true) == gl_RayQueryCommittedIntersectionTriangleEXT)
		{
			// Get information from the intersection
			const HitInfo hitInfo = getObjectHitInfo(rayQuery);

			// Accumulate color.
			curAttenuation *= hitInfo.color;

			// Update the ray's origin and scatter direction for the next bounce.
			// Apply a small offset to the origin to avoid self-intersections.
			ray.origin = hitInfo.worldPosition - 0.0001f * sign(dot(ray.direction, hitInfo.worldNormal)) * hitInfo.worldNormal;

			const float theta = 6.2831853 * stepAndOutputRNGFloat(rngState);   // Random in [0, 2pi]
			const float u = 2.0 * stepAndOutputRNGFloat(rngState) - 1.0;  // Random in [-1, 1]
			const float r = sqrt(1.0 - u * u);
			ray.direction = normalize(hitInfo.worldNormal + vec3(r * cos(theta), r * sin(theta), u));
		}
	}
	// Exceeded recursion - assume the sample provides no contribution to the light.
	return vec3(0.f);
	
}


vec3 skyColor(vec3 direction)
{
	if (direction.y > 0.0f) {
		return mix(vec3(1.0f), vec3(0.25f, 0.5f, 1.0f), direction.y);
	}
	// Fix a color for the floor.
	else{
		return vec3(0.03f);
	}
}


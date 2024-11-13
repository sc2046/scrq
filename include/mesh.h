 #pragma once

#include <host_device_common.h>
#include <vk_helpers.h>
#include <vk_types.h>

#include "tiny_obj_loader.h"


struct ObjMesh
{
	std::vector<Vertex>		mVertices; 
	std::vector<uint32_t>   mIndices;
	glm::mat4				mTransform = glm::mat4(1.f);
	uint32_t				mMaterialID;

    AllocatedBuffer			mVertexBuffer;
	AllocatedBuffer			mIndexBuffer;
	
	AccelerationStructure	mBlas;          // TODO: Should be a vector, one per primitive?

	bool loadFromFile(const fs::path& path)
	{

        tinyobj::ObjReader       reader; 

        reader.ParseFromFile(path.string().c_str());
        assert(reader.Valid());  

		auto& attrib = reader.GetAttrib();
		auto& shapes = reader.GetShapes();

		// Parse all vertices, normals, and texture coordinates
		for (const auto& shape : shapes) {
			for (const auto& index : shape.mesh.indices) {
				Vertex vertex;

				// Position
				vertex.position.x = attrib.vertices[3 * index.vertex_index + 0];
				vertex.position.y = attrib.vertices[3 * index.vertex_index + 1];
				vertex.position.z = attrib.vertices[3 * index.vertex_index + 2];

				// Normal
				if (index.normal_index >= 0) {
					vertex.normal.x = attrib.normals[3 * index.normal_index + 0];
					vertex.normal.y = attrib.normals[3 * index.normal_index + 1];
					vertex.normal.z = attrib.normals[3 * index.normal_index + 2];
				}
				else { vertex.normal.x = vertex.normal.y = vertex.normal.z = 0.f; }
				// Texture Coordinate
				if (index.texcoord_index >= 0) { // Check if texture coordinate exists
					vertex.tex.x = attrib.texcoords[2 * index.texcoord_index + 0];
					vertex.tex.y = attrib.texcoords[2 * index.texcoord_index + 1];
				}
				else {vertex.tex.x = vertex.tex.y = 0.0f;}

				mVertices.push_back(std::move(vertex));
				mIndices.push_back(static_cast<uint32_t>(mVertices.size() - 1));
			}
		}

        return true;
	}
};
#pragma once

#include <vk_types.h>

#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

struct Vertex
{
	glm::vec3 position;
	glm::vec3 normal;
	glm::vec2 tex;
};

struct ObjMesh
{
	std::vector<Vertex> mVertices;
	std::vector<uint32_t> mIndices;

	//bool loadFromFile(const fs::path& path)
	//{
 //       tinyobj::ObjReader       reader;  // Used to read an OBJ file
 //       //reader.ParseFromFile("assets/sphere.obj");
 //       reader.ParseFromFile("assets/cornell.obj");
 //       assert(reader.Valid());  // Make sure tinyobj was able to parse this file

 //       std::vector<tinyobj::real_t>   objVertices = reader.GetAttrib().GetVertices();

 //       const  std::vector<tinyobj::shape_t>& objShapes = reader.GetShapes();  // All shapes in the file
 //       assert(objShapes.size() == 1);                                          // Check that this file has only one shape
 //       const tinyobj::shape_t& objShape = objShapes[0];                        // Get the first shape




 //       // Get the indices of the vertices of the first mesh of `objShape` in `attrib.vertices`:
 //       std::vector<uint32_t> objIndices;
 //       objIndices.reserve(objShape.mesh.indices.size());
 //       for (const tinyobj::index_t& index : objShape.mesh.indices)
 //       {
 //           objIndices.push_back(index.vertex_index);
 //       }

 //       return VulkanApp::Mesh{ .mVertices = std::move(objVertices), .mIndices = std::move(objIndices) };
	//}
};
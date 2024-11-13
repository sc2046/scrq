#pragma once

#include <host_device_common.h>
#include <vk_helpers.h>

#include <cstdlib>

inline float random_double() {
    // Returns a random real in [0,1).
    return std::rand() / (RAND_MAX + 1.0);
}

inline glm::vec3 random_vector() {
    return vec3(random_double(), random_double(), random_double());
}

struct Sphere
{
    vec3	center;
    float	radius;
    uint	materialID;
};

struct Texture
{
    VkImage     mImage;
    VkImageView mImageView;
    VkExtent3D  mExtents;       // Width, height, depth
    VkFormat    mFormat;        // e.g. R32G32B32A32.
};

struct Scene
{
    std::string                         mName;
    Camera				                mCamera;

	std::vector<Sphere>                 mSpheres;
    std::vector<ObjMesh>                mMeshes;

    //std::vector<Texture>                mTextures;

    std::vector<Material>               mMaterials;
    AllocatedBuffer                     mMaterialsBuffer;

    //Integrator                        mIntegrator;

};


inline Scene createShirleyBook1Scene()
{
    Scene scene;
    scene.mName = "ShirleyBook1";

    scene.mCamera = {
        .center = glm::vec3(13.f, 2.f, 3.f),
        .eye = glm::vec3(0.f,0.f,0.f),
        .backgroundColor = glm::vec3(1.f),
        .fovY = 20.f,
        .focalDistance = 1.f
    };


    scene.mMaterials.emplace_back(Material{ .type = DIFFUSE, .albedo = glm::vec3(0.5f), .emitted = glm::vec3(0.f) });

    scene.mMeshes.resize(1);
    scene.mMeshes[0].loadFromFile("assets/xy_quad.obj");
    auto transform = glm::translate(glm::mat4(1.f), { 0.f,0,0.f });
    transform = glm::rotate(transform, glm::radians(-90.f), glm::vec3(1.f, 0.f, 0.f));
    transform = glm::scale(transform, glm::vec3(1000));
    scene.mMeshes[0].mTransform = transform;
    scene.mMeshes[0].mMaterialID = scene.mMaterials.size()-1;


    for (int a = -11; a < 11; a++) {
        for (int b = -11; b < 11; b++) {
            const auto choose_mat = random_double();

            glm::vec3 center(a + 0.9 * random_double(), 0.2, b + 0.9 * random_double());

            if (glm::length(center - glm::vec3(4, 0.2, 0)) > 0.9) {

                if (choose_mat < 0.8) {
                    scene.mMaterials.emplace_back(Material{ .type = DIFFUSE, .albedo = random_vector(), .emitted = glm::vec3(0.f) });
                    scene.mSpheres.emplace_back(center, 0.2f, scene.mMaterials.size()-1);
                }
                else if (choose_mat < 0.95) {
                    scene.mMaterials.emplace_back(Material{ .type = MIRROR, .albedo = random_vector(), .emitted = glm::vec3(0.f) });
                    scene.mSpheres.emplace_back(center, 0.2f, scene.mMaterials.size() - 1);
                }
                else {
                    scene.mMaterials.emplace_back(Material{ .type = DIELECTRIC, .albedo = glm::vec3(1.f), .emitted = glm::vec3(0.f)});
                    scene.mSpheres.emplace_back(center, 0.2f, scene.mMaterials.size() - 1);
                }
            }
        }
    }
    scene.mMaterials.emplace_back(Material{ .type = DIELECTRIC, .albedo = glm::vec3(1.f), .emitted = glm::vec3(0.f) });
    scene.mSpheres.emplace_back(glm::vec3(0, 1, 0), 1.f, scene.mMaterials.size() - 1);

    scene.mMaterials.emplace_back(Material{ .type = DIFFUSE, .albedo = glm::vec3(0.4f, 0.2f, 0.1f), .emitted = glm::vec3(0.f) });
    scene.mSpheres.emplace_back(glm::vec3(0, 1, 0), 1.f, scene.mMaterials.size() - 1);

    scene.mMaterials.emplace_back(Material{ .type = MIRROR, .albedo = glm::vec3(0.7, 0.6, 0.5), .emitted = glm::vec3(0.f) });
    scene.mSpheres.emplace_back(glm::vec3(4, 1, 0), 1.f, scene.mMaterials.size() - 1);

    return scene;
}

inline Scene createSponzaBuddhaScene()
{
    Scene scene;
    scene.mName = "Sponza";

    scene.mCamera = {
        .center = glm::vec3(-2, 0.5, -0.1),
        .eye = glm::vec3(0, 0.5, 0),
        .backgroundColor = glm::vec3(2.f),
        .fovY = 90.f,
        .focalDistance = 1.f
    };

    scene.mMaterials.emplace_back(Material{ .type = DIFFUSE,  .albedo = glm::vec3(0.5f), .emitted = glm::vec3(0.f) });

    scene.mMeshes.resize(1);
    scene.mMeshes[0].loadFromFile("assets/sponza.obj");
    scene.mMeshes[0].mMaterialID = 0;

    return scene;
}

inline Scene createAjaxScene()
{
    Scene scene;
    scene.mName = "Ajax";

    scene.mCamera = {
        .center = glm::vec3(-65.6055, 47.5762, 24.3583),
        .eye = glm::vec3(-64.8161, 47.2211, 23.8576),
        .backgroundColor = glm::vec3(5.f),
        .fovY = 30.f,
        .focalDistance = 1.f
    };

    scene.mMaterials.emplace_back(Material{ .type = DIFFUSE,  .albedo = glm::vec3(0.2f), .emitted = glm::vec3(0.f) });


    scene.mMeshes.resize(2);
    scene.mMeshes[0].loadFromFile("assets/xy_quad.obj");
    auto transform = glm::translate(glm::mat4(1.f), { 0.f,0,0.f });
    transform = glm::rotate(transform, glm::radians(-90.f), glm::vec3(1.f, 0.f, 0.f));
    transform = glm::scale(transform, glm::vec3(1000));
    scene.mMeshes[0].mTransform = transform;
    scene.mMeshes[0].mMaterialID = 0;

    scene.mMeshes[1].loadFromFile("assets/ajax.obj");
    scene.mMeshes[1].mMaterialID = 0;

    return scene;
}

inline Scene createSphereCornellBoxScene()
{
    Scene scene;
    scene.mName = "SphereCornellBox";

    scene.mCamera = {
        .center = glm::vec3(0, 20, 1077.5),
        .eye = glm::vec3(0, -4, 0),
        .backgroundColor = glm::vec3(0.f),
        .fovY = 40,
        .focalDistance = 1.f
    };


    scene.mMaterials.emplace_back(Material{ .type = DIFFUSE, .albedo = glm::vec3(0.73), .emitted = glm::vec3(0.f) }); // white
    scene.mMaterials.emplace_back(Material{ .type = DIFFUSE, .albedo = glm::vec3(0.65, 0.05, 0.05), .emitted = glm::vec3(0.f) }); // red
    scene.mMaterials.emplace_back(Material{ .type = DIFFUSE, .albedo = glm::vec3(0.12, 0.45, 0.15), .emitted = glm::vec3(0.f) }); // green
    scene.mMaterials.emplace_back(Material{ .type = LIGHT, .albedo = glm::vec3(1.f), .emitted = glm::vec3(15) }); // light


    scene.mMaterials.emplace_back(Material{ .type = DIFFUSE, .albedo = glm::vec3(0.75, 0.25, 0.25), .emitted = glm::vec3(0.f) }); // red sphere
    scene.mMaterials.emplace_back(Material{ .type = DIFFUSE, .albedo = glm::vec3(0.25, 0.75, 0.25), .emitted = glm::vec3(0.f) }); // green sphere



    scene.mMeshes.resize(6);

    scene.mMeshes[0].loadFromFile("assets/xy_quad.obj");
    auto transform  = glm::translate(glm::mat4(1.f), glm::vec3(0, 0, -277.5));
    transform       = glm::scale(transform, glm::vec3(555));
    scene.mMeshes[0].mTransform = transform;
    scene.mMeshes[0].mMaterialID = 0;


    scene.mMeshes[1].loadFromFile("assets/xy_quad.obj");
    transform   = glm::translate(glm::mat4(1.f),  glm::vec3(0, 277.5, 0));
    transform   = glm::rotate(transform, glm::radians(90.f), glm::vec3(1.f, 0.f, 0.f));
    transform   = glm::scale(transform, glm::vec3(555));
    scene.mMeshes[1].mTransform = transform;
    scene.mMeshes[1].mMaterialID = 0;


    scene.mMeshes[2].loadFromFile("assets/xy_quad.obj");
    transform = glm::translate(glm::mat4(1.f), glm::vec3(0, -277.5, 0));
    transform = glm::rotate(transform, glm::radians(-90.f), glm::vec3(1.f, 0.f, 0.f));
    transform = glm::scale(transform, glm::vec3(555));
    scene.mMeshes[2].mTransform = transform;
    scene.mMeshes[2].mMaterialID = 0;


    scene.mMeshes[3].loadFromFile("assets/xy_quad.obj");
    transform = glm::translate(glm::mat4(1.f), glm::vec3(-277.5, 0, 0));
    transform = glm::rotate(transform, glm::radians(90.f), glm::vec3(0.f, 1.f, 0.f));
    transform = glm::scale(transform, glm::vec3(555));
    scene.mMeshes[3].mTransform = transform;
    scene.mMeshes[3].mMaterialID = 2;

    scene.mMeshes[4].loadFromFile("assets/xy_quad.obj");
    transform = glm::translate(glm::mat4(1.f), glm::vec3(277.5, 0, 0));
    transform = glm::rotate(transform, glm::radians(-90.f), glm::vec3(0.f, 1.f, 0.f));
    transform = glm::scale(transform, glm::vec3(555));
    scene.mMeshes[4].mTransform = transform;
    scene.mMeshes[4].mMaterialID = 1;


    // Light Source
    scene.mMeshes[5].loadFromFile("assets/xy_quad.obj");
    transform = glm::translate(glm::mat4(1.f), glm::vec3(0, 277, 0));
    transform = glm::rotate(transform, glm::radians(90.f), glm::vec3(1.f, 0.f, 0.f));
    transform = glm::scale(transform, glm::vec3(130));
    scene.mMeshes[5].mTransform = transform;
    scene.mMeshes[5].mMaterialID = 3;

    scene.mSpheres.emplace_back(glm::vec3(-140, -177.5, -100), 100.f, 4);
    scene.mSpheres.emplace_back(glm::vec3(140, -177.5, 100), 100.f, 5);

    return scene;
}

inline Scene createBuddhaCornellBox()
{
    Scene scene;
    scene.mName = "BuddhaCornellBox";

    scene.mCamera = {
        .center = glm::vec3(0, 20, 1077.5),
        .eye = glm::vec3(0, -4, 0),
        .backgroundColor = glm::vec3(0.f),
        .fovY = 40,
        .focalDistance = 1.f
    };

    scene.mMaterials.emplace_back(Material{ .type = DIFFUSE, .albedo = glm::vec3(0.73), .emitted = glm::vec3(0.f) }); // white
    scene.mMaterials.emplace_back(Material{ .type = DIFFUSE, .albedo = glm::vec3(0.65, 0.05, 0.05), .emitted = glm::vec3(0.f) }); // red
    scene.mMaterials.emplace_back(Material{ .type = DIFFUSE, .albedo = glm::vec3(0.12, 0.45, 0.15), .emitted = glm::vec3(0.f) }); // green
    scene.mMaterials.emplace_back(Material{ .type = LIGHT, .albedo = glm::vec3(1.f), .emitted = glm::vec3(15) }); // light
    scene.mMaterials.emplace_back(Material{ .type = DIELECTRIC, .albedo = glm::vec3(1.f), .emitted = glm::vec3(0) }); // light

    scene.mMeshes.resize(8);

    scene.mMeshes[0].loadFromFile("assets/xy_quad.obj");
    auto transform = glm::translate(glm::mat4(1.f), glm::vec3(0, 0, -277.5));
    transform = glm::scale(transform, glm::vec3(555));
    scene.mMeshes[0].mTransform = transform;
    scene.mMeshes[0].mMaterialID = 0;


    scene.mMeshes[1].loadFromFile("assets/xy_quad.obj");
    transform = glm::translate(glm::mat4(1.f), glm::vec3(0, 277.5, 0));
    transform = glm::rotate(transform, glm::radians(90.f), glm::vec3(1.f, 0.f, 0.f));
    transform = glm::scale(transform, glm::vec3(555));
    scene.mMeshes[1].mTransform = transform;
    scene.mMeshes[1].mMaterialID = 0;


    scene.mMeshes[2].loadFromFile("assets/xy_quad.obj");
    transform = glm::translate(glm::mat4(1.f), glm::vec3(0, -277.5, 0));
    transform = glm::rotate(transform, glm::radians(-90.f), glm::vec3(1.f, 0.f, 0.f));
    transform = glm::scale(transform, glm::vec3(555));
    scene.mMeshes[2].mTransform = transform;
    scene.mMeshes[2].mMaterialID = 0;


    scene.mMeshes[3].loadFromFile("assets/xy_quad.obj");
    transform = glm::translate(glm::mat4(1.f), glm::vec3(-277.5, 0, 0));
    transform = glm::rotate(transform, glm::radians(90.f), glm::vec3(0.f, 1.f, 0.f));
    transform = glm::scale(transform, glm::vec3(555));
    scene.mMeshes[3].mTransform = transform;
    scene.mMeshes[3].mMaterialID = 2;

    scene.mMeshes[4].loadFromFile("assets/xy_quad.obj");
    transform = glm::translate(glm::mat4(1.f), glm::vec3(277.5, 0, 0));
    transform = glm::rotate(transform, glm::radians(-90.f), glm::vec3(0.f, 1.f, 0.f));
    transform = glm::scale(transform, glm::vec3(555));
    scene.mMeshes[4].mTransform = transform;
    scene.mMeshes[4].mMaterialID = 1;


    // Light Source
    scene.mMeshes[5].loadFromFile("assets/xy_quad.obj");
    transform = glm::translate(glm::mat4(1.f), glm::vec3(0, 277, 0));
    transform = glm::rotate(transform, glm::radians(90.f), glm::vec3(1.f, 0.f, 0.f));
    transform = glm::scale(transform, glm::vec3(130));
    scene.mMeshes[5].mTransform = transform;
    scene.mMeshes[5].mMaterialID = 3;


    // Diffuse Buddha
    scene.mMeshes[6].loadFromFile("assets/buddha.obj");
    transform = glm::translate(glm::mat4(1.f), glm::vec3(-140, -277.5, -100));
    transform = glm::rotate(transform, glm::radians(90.f), glm::vec3(0.f, 1.f, 0.f));
    transform = glm::scale(transform, glm::vec3(500));
    transform = glm::translate(transform, glm::vec3(-1.02949, 0.006185, -0.03784));
    scene.mMeshes[6].mTransform = transform;
    scene.mMeshes[6].mMaterialID = 0;

    // Dielectric Buddha
    scene.mMeshes[7].loadFromFile("assets/buddha.obj");
    transform = glm::translate(glm::mat4(1.f), glm::vec3(140, -277.5, 100));
    transform = glm::rotate(transform, glm::radians(90.f), glm::vec3(0.f, 1.f, 0.f));
    transform = glm::scale(transform, glm::vec3(500));
    transform = glm::translate(transform, glm::vec3(-1.02949, 0.006185, -0.03784));
    scene.mMeshes[7].mTransform = transform;
    scene.mMeshes[7].mMaterialID = 4;

    return scene;
}

inline Scene createVeachMatsScene()
{
    Scene scene;
    scene.mName = "Veach_Mats";

    scene.mCamera = {
        .center = glm::vec3(0, 6, 27.5),
        .eye = glm::vec3(0, -1.5, 2.5),
        .backgroundColor = glm::vec3(0.f),
        .fovY = 16,
        .focalDistance = 1.f
    };


    scene.mMaterials.emplace_back(Material{ .type = LIGHT, .emitted = glm::vec3(901) });
    scene.mMaterials.emplace_back(Material{ .type = LIGHT, .emitted = glm::vec3(100) });
    scene.mMaterials.emplace_back(Material{ .type = LIGHT, .emitted = glm::vec3(11) });
    scene.mMaterials.emplace_back(Material{ .type = LIGHT, .emitted = glm::vec3(1.2) });




    scene.mSpheres.emplace_back(Sphere{ .center = glm::vec3(3.75, 0, 0), .radius = 0.03333,.materialID = 0 });
    scene.mSpheres.emplace_back(Sphere{ .center = glm::vec3(1.25, 0, 0), .radius = 0.1, .materialID = 1 });
    scene.mSpheres.emplace_back(Sphere{ .center = glm::vec3(-1.25, 0, 0), .radius = 0.3, .materialID = 2 });
    scene.mSpheres.emplace_back(Sphere{ .center = glm::vec3(-3.75, 0, 0), .radius = 0.9, .materialID = 3 });

    scene.mMaterials.emplace_back(Material{ .type = PHONG, .albedo = glm::vec3(0.35), .phongExponent = 100000 });
    scene.mMaterials.emplace_back(Material{ .type = PHONG, .albedo = glm::vec3(0.25), .phongExponent = 5000 });
    scene.mMaterials.emplace_back(Material{ .type = PHONG, .albedo = glm::vec3(0.2), .phongExponent = 400 });
    scene.mMaterials.emplace_back(Material{ .type = PHONG, .albedo = glm::vec3(0.2), .phongExponent = 100 });
    scene.mMaterials.emplace_back(Material{ .type = DIFFUSE, .albedo = glm::vec3(0.2) });


    scene.mMeshes.resize(5);
    scene.mMeshes[0].loadFromFile("assets/veach/plate1.obj");
    scene.mMeshes[0].mMaterialID = 4;

    scene.mMeshes[1].loadFromFile("assets/veach/plate2.obj");
    scene.mMeshes[1].mMaterialID = 5;

    scene.mMeshes[2].loadFromFile("assets/veach/plate3.obj");
    scene.mMeshes[2].mMaterialID = 6;

    scene.mMeshes[3].loadFromFile("assets/veach/plate4.obj");
    scene.mMeshes[3].mMaterialID = 7;


    scene.mMeshes[4].loadFromFile("assets/veach/floor.obj");
    scene.mMeshes[4].mMaterialID = 8;

    return scene;
}

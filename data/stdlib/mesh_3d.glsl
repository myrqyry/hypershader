// HyperShader Standard Library - 3D Mesh Rendering
// Advanced mesh loading, processing, and rendering beyond SDFs

#ifndef _STDLIB_MESH_3D_H_
#define _STDLIB_MESH_3D_H_

// === MESH DATA STRUCTURES ===

// Vertex structure
struct Vertex3D {
    float3 position;
    float3 normal;
    float2 uv;
    float3 tangent;
    float3 bitangent;
    float4 color;
};

// Triangle structure
struct Triangle3D {
    Vertex3D vertices[3];
    float3 normal;
    float area;
};

// Mesh structure
struct Mesh3D {
    Triangle3D triangles[128]; // Reduced from 512 to prevent register pressure
    int triangle_count;
    float3 bounds_min;
    float3 bounds_max;
    float3 center;
    float radius;
};

// Instance structure for instanced rendering
struct MeshInstance3D {
    float4x4 transform;
    float3 color;
    float metallic;
    float roughness;
    bool visible;
};

// === MESH CREATION UTILITIES ===

// Create a simple cube mesh
Mesh3D createCubeMesh(float size) {
    Mesh3D mesh;
    mesh.triangle_count = 12; // 6 faces * 2 triangles each

    float halfSize = size * 0.5;
    float3 positions[8] = {
        float3(-halfSize, -halfSize, -halfSize), // 0: bottom-back-left
        float3( halfSize, -halfSize, -halfSize), // 1: bottom-back-right
        float3( halfSize,  halfSize, -halfSize), // 2: top-back-right
        float3(-halfSize,  halfSize, -halfSize), // 3: top-back-left
        float3(-halfSize, -halfSize,  halfSize), // 4: bottom-front-left
        float3( halfSize, -halfSize,  halfSize), // 5: bottom-front-right
        float3( halfSize,  halfSize,  halfSize), // 6: top-front-right
        float3(-halfSize,  halfSize,  halfSize)  // 7: top-front-left
    };

    float2 uvs[4] = {
        float2(0.0, 0.0),
        float2(1.0, 0.0),
        float2(1.0, 1.0),
        float2(0.0, 1.0)
    };

    // Front face (Z+)
    mesh.triangles[0].vertices[0] = Vertex3D(positions[4], float3(0.0, 0.0, 1.0), uvs[0], float3(1.0, 0.0, 0.0), float3(0.0, 1.0, 0.0), float4(1.0));
    mesh.triangles[0].vertices[1] = Vertex3D(positions[5], float3(0.0, 0.0, 1.0), uvs[1], float3(1.0, 0.0, 0.0), float3(0.0, 1.0, 0.0), float4(1.0));
    mesh.triangles[0].vertices[2] = Vertex3D(positions[6], float3(0.0, 0.0, 1.0), uvs[2], float3(1.0, 0.0, 0.0), float3(0.0, 1.0, 0.0), float4(1.0));

    mesh.triangles[1].vertices[0] = Vertex3D(positions[4], float3(0.0, 0.0, 1.0), uvs[0], float3(1.0, 0.0, 0.0), float3(0.0, 1.0, 0.0), float4(1.0));
    mesh.triangles[1].vertices[1] = Vertex3D(positions[6], float3(0.0, 0.0, 1.0), uvs[2], float3(1.0, 0.0, 0.0), float3(0.0, 1.0, 0.0), float4(1.0));
    mesh.triangles[1].vertices[2] = Vertex3D(positions[7], float3(0.0, 0.0, 1.0), uvs[3], float3(1.0, 0.0, 0.0), float3(0.0, 1.0, 0.0), float4(1.0));

    // Back face (Z-)
    mesh.triangles[2].vertices[0] = Vertex3D(positions[1], float3(0.0, 0.0, -1.0), uvs[0], float3(-1.0, 0.0, 0.0), float3(0.0, 1.0, 0.0), float4(1.0));
    mesh.triangles[2].vertices[1] = Vertex3D(positions[0], float3(0.0, 0.0, -1.0), uvs[1], float3(-1.0, 0.0, 0.0), float3(0.0, 1.0, 0.0), float4(1.0));
    mesh.triangles[2].vertices[2] = Vertex3D(positions[3], float3(0.0, 0.0, -1.0), uvs[2], float3(-1.0, 0.0, 0.0), float3(0.0, 1.0, 0.0), float4(1.0));

    mesh.triangles[3].vertices[0] = Vertex3D(positions[1], float3(0.0, 0.0, -1.0), uvs[0], float3(-1.0, 0.0, 0.0), float3(0.0, 1.0, 0.0), float4(1.0));
    mesh.triangles[3].vertices[1] = Vertex3D(positions[3], float3(0.0, 0.0, -1.0), uvs[2], float3(-1.0, 0.0, 0.0), float3(0.0, 1.0, 0.0), float4(1.0));
    mesh.triangles[3].vertices[2] = Vertex3D(positions[2], float3(0.0, 0.0, -1.0), uvs[3], float3(-1.0, 0.0, 0.0), float3(0.0, 1.0, 0.0), float4(1.0));

    // Left face (X-)
    mesh.triangles[4].vertices[0] = Vertex3D(positions[0], float3(-1.0, 0.0, 0.0), uvs[0], float3(0.0, 0.0, -1.0), float3(0.0, 1.0, 0.0), float4(1.0));
    mesh.triangles[4].vertices[1] = Vertex3D(positions[4], float3(-1.0, 0.0, 0.0), uvs[1], float3(0.0, 0.0, -1.0), float3(0.0, 1.0, 0.0), float4(1.0));
    mesh.triangles[4].vertices[2] = Vertex3D(positions[7], float3(-1.0, 0.0, 0.0), uvs[2], float3(0.0, 0.0, -1.0), float3(0.0, 1.0, 0.0), float4(1.0));

    mesh.triangles[5].vertices[0] = Vertex3D(positions[0], float3(-1.0, 0.0, 0.0), uvs[0], float3(0.0, 0.0, -1.0), float3(0.0, 1.0, 0.0), float4(1.0));
    mesh.triangles[5].vertices[1] = Vertex3D(positions[7], float3(-1.0, 0.0, 0.0), uvs[2], float3(0.0, 0.0, -1.0), float3(0.0, 1.0, 0.0), float4(1.0));
    mesh.triangles[5].vertices[2] = Vertex3D(positions[3], float3(-1.0, 0.0, 0.0), uvs[3], float3(0.0, 0.0, -1.0), float3(0.0, 1.0, 0.0), float4(1.0));

    // Right face (X+)
    mesh.triangles[6].vertices[0] = Vertex3D(positions[5], float3(1.0, 0.0, 0.0), uvs[0], float3(0.0, 0.0, 1.0), float3(0.0, 1.0, 0.0), float4(1.0));
    mesh.triangles[6].vertices[1] = Vertex3D(positions[1], float3(1.0, 0.0, 0.0), uvs[1], float3(0.0, 0.0, 1.0), float3(0.0, 1.0, 0.0), float4(1.0));
    mesh.triangles[6].vertices[2] = Vertex3D(positions[2], float3(1.0, 0.0, 0.0), uvs[2], float3(0.0, 0.0, 1.0), float3(0.0, 1.0, 0.0), float4(1.0));

    mesh.triangles[7].vertices[0] = Vertex3D(positions[5], float3(1.0, 0.0, 0.0), uvs[0], float3(0.0, 0.0, 1.0), float3(0.0, 1.0, 0.0), float4(1.0));
    mesh.triangles[7].vertices[1] = Vertex3D(positions[2], float3(1.0, 0.0, 0.0), uvs[2], float3(0.0, 0.0, 1.0), float3(0.0, 1.0, 0.0), float4(1.0));
    mesh.triangles[7].vertices[2] = Vertex3D(positions[6], float3(1.0, 0.0, 0.0), uvs[3], float3(0.0, 0.0, 1.0), float3(0.0, 1.0, 0.0), float4(1.0));

    // Bottom face (Y-)
    mesh.triangles[8].vertices[0] = Vertex3D(positions[0], float3(0.0, -1.0, 0.0), uvs[0], float3(1.0, 0.0, 0.0), float3(0.0, 0.0, -1.0), float4(1.0));
    mesh.triangles[8].vertices[1] = Vertex3D(positions[1], float3(0.0, -1.0, 0.0), uvs[1], float3(1.0, 0.0, 0.0), float3(0.0, 0.0, -1.0), float4(1.0));
    mesh.triangles[8].vertices[2] = Vertex3D(positions[5], float3(0.0, -1.0, 0.0), uvs[2], float3(1.0, 0.0, 0.0), float3(0.0, 0.0, -1.0), float4(1.0));

    mesh.triangles[9].vertices[0] = Vertex3D(positions[0], float3(0.0, -1.0, 0.0), uvs[0], float3(1.0, 0.0, 0.0), float3(0.0, 0.0, -1.0), float4(1.0));
    mesh.triangles[9].vertices[1] = Vertex3D(positions[5], float3(0.0, -1.0, 0.0), uvs[2], float3(1.0, 0.0, 0.0), float3(0.0, 0.0, -1.0), float4(1.0));
    mesh.triangles[9].vertices[2] = Vertex3D(positions[4], float3(0.0, -1.0, 0.0), uvs[3], float3(1.0, 0.0, 0.0), float3(0.0, 0.0, -1.0), float4(1.0));

    // Top face (Y+)
    mesh.triangles[10].vertices[0] = Vertex3D(positions[3], float3(0.0, 1.0, 0.0), uvs[0], float3(1.0, 0.0, 0.0), float3(0.0, 0.0, 1.0), float4(1.0));
    mesh.triangles[10].vertices[1] = Vertex3D(positions[7], float3(0.0, 1.0, 0.0), uvs[1], float3(1.0, 0.0, 0.0), float3(0.0, 0.0, 1.0), float4(1.0));
    mesh.triangles[10].vertices[2] = Vertex3D(positions[6], float3(0.0, 1.0, 0.0), uvs[2], float3(1.0, 0.0, 0.0), float3(0.0, 0.0, 1.0), float4(1.0));

    mesh.triangles[11].vertices[0] = Vertex3D(positions[3], float3(0.0, 1.0, 0.0), uvs[0], float3(1.0, 0.0, 0.0), float3(0.0, 0.0, 1.0), float4(1.0));
    mesh.triangles[11].vertices[1] = Vertex3D(positions[6], float3(0.0, 1.0, 0.0), uvs[2], float3(1.0, 0.0, 0.0), float3(0.0, 0.0, 1.0), float4(1.0));
    mesh.triangles[11].vertices[2] = Vertex3D(positions[2], float3(0.0, 1.0, 0.0), uvs[3], float3(1.0, 0.0, 0.0), float3(0.0, 0.0, 1.0), float4(1.0));

    // Calculate bounds
    mesh.bounds_min = float3(-halfSize);
    mesh.bounds_max = float3(halfSize);
    mesh.center = float3(0.0);
    mesh.radius = length(float3(halfSize));

    // Calculate triangle normals and areas
    for (int i = 0; i < mesh.triangle_count; i++) {
        Triangle3D tri = mesh.triangles[i];
        float3 e1 = tri.vertices[1].position - tri.vertices[0].position;
        float3 e2 = tri.vertices[2].position - tri.vertices[0].position;
        tri.normal = normalize(cross(e1, e2));
        tri.area = length(cross(e1, e2)) * 0.5;
        mesh.triangles[i] = tri;
    }

    return mesh;
}

// Create a sphere mesh (icosahedral approximation)
Mesh3D createSphereMesh(float radius, int subdivisions) {
    Mesh3D mesh;
    // This would be complex to implement fully here
    // For now, return a simple octahedron sphere approximation
    mesh.triangle_count = 8;

    float r = radius;
    float3 positions[6] = {
        float3(0.0, r, 0.0),   // Top
        float3(0.0, -r, 0.0),  // Bottom
        float3(-r, 0.0, 0.0),  // Left
        float3(r, 0.0, 0.0),   // Right
        float3(0.0, 0.0, r),   // Front
        float3(0.0, 0.0, -r)   // Back
    };

    // Create 8 triangles for octahedron
    int triIndex = 0;

    // Top pyramid
    mesh.triangles[triIndex++] = Triangle3D(
        Vertex3D(positions[0], normalize(positions[0]), float2(0.5, 1.0), float3(1.0, 0.0, 0.0), float3(0.0, 0.0, 1.0), float4(1.0)),
        Vertex3D(positions[2], normalize(positions[2]), float2(0.0, 0.5), float3(1.0, 0.0, 0.0), float3(0.0, 0.0, 1.0), float4(1.0)),
        Vertex3D(positions[4], normalize(positions[4]), float2(0.5, 0.0), float3(1.0, 0.0, 0.0), float3(0.0, 0.0, 1.0), float4(1.0))
    );

    mesh.triangles[triIndex++] = Triangle3D(
        Vertex3D(positions[0], normalize(positions[0]), float2(0.5, 1.0), float3(1.0, 0.0, 0.0), float3(0.0, 0.0, 1.0), float4(1.0)),
        Vertex3D(positions[4], normalize(positions[4]), float2(0.5, 0.0), float3(1.0, 0.0, 0.0), float3(0.0, 0.0, 1.0), float4(1.0)),
        Vertex3D(positions[3], normalize(positions[3]), float2(1.0, 0.5), float3(1.0, 0.0, 0.0), float3(0.0, 0.0, 1.0), float4(1.0))
    );

    mesh.triangles[triIndex++] = Triangle3D(
        Vertex3D(positions[0], normalize(positions[0]), float2(0.5, 1.0), float3(1.0, 0.0, 0.0), float3(0.0, 0.0, 1.0), float4(1.0)),
        Vertex3D(positions[3], normalize(positions[3]), float2(1.0, 0.5), float3(1.0, 0.0, 0.0), float3(0.0, 0.0, 1.0), float4(1.0)),
        Vertex3D(positions[5], normalize(positions[5]), float2(0.5, 0.0), float3(1.0, 0.0, 0.0), float3(0.0, 0.0, 1.0), float4(1.0))
    );

    mesh.triangles[triIndex++] = Triangle3D(
        Vertex3D(positions[0], normalize(positions[0]), float2(0.5, 1.0), float3(1.0, 0.0, 0.0), float3(0.0, 0.0, 1.0), float4(1.0)),
        Vertex3D(positions[5], normalize(positions[5]), float2(0.5, 0.0), float3(1.0, 0.0, 0.0), float3(0.0, 0.0, 1.0), float4(1.0)),
        Vertex3D(positions[2], normalize(positions[2]), float2(0.0, 0.5), float3(1.0, 0.0, 0.0), float3(0.0, 0.0, 1.0), float4(1.0))
    );

    // Bottom pyramid
    mesh.triangles[triIndex++] = Triangle3D(
        Vertex3D(positions[1], normalize(positions[1]), float2(0.5, 0.0), float3(1.0, 0.0, 0.0), float3(0.0, 0.0, -1.0), float4(1.0)),
        Vertex3D(positions[4], normalize(positions[4]), float2(0.5, 1.0), float3(1.0, 0.0, 0.0), float3(0.0, 0.0, -1.0), float4(1.0)),
        Vertex3D(positions[2], normalize(positions[2]), float2(0.0, 0.5), float3(1.0, 0.0, 0.0), float3(0.0, 0.0, -1.0), float4(1.0))
    );

    mesh.triangles[triIndex++] = Triangle3D(
        Vertex3D(positions[1], normalize(positions[1]), float2(0.5, 0.0), float3(1.0, 0.0, 0.0), float3(0.0, 0.0, -1.0), float4(1.0)),
        Vertex3D(positions[3], normalize(positions[3]), float2(1.0, 0.5), float3(1.0, 0.0, 0.0), float3(0.0, 0.0, -1.0), float4(1.0)),
        Vertex3D(positions[4], normalize(positions[4]), float2(0.5, 1.0), float3(1.0, 0.0, 0.0), float3(0.0, 0.0, -1.0), float4(1.0))
    );

    mesh.triangles[triIndex++] = Triangle3D(
        Vertex3D(positions[1], normalize(positions[1]), float2(0.5, 0.0), float3(1.0, 0.0, 0.0), float3(0.0, 0.0, -1.0), float4(1.0)),
        Vertex3D(positions[5], normalize(positions[5]), float2(0.5, 1.0), float3(1.0, 0.0, 0.0), float3(0.0, 0.0, -1.0), float4(1.0)),
        Vertex3D(positions[3], normalize(positions[3]), float2(1.0, 0.5), float3(1.0, 0.0, 0.0), float3(0.0, 0.0, -1.0), float4(1.0))
    );

    mesh.triangles[triIndex++] = Triangle3D(
        Vertex3D(positions[1], normalize(positions[1]), float2(0.5, 0.0), float3(1.0, 0.0, 0.0), float3(0.0, 0.0, -1.0), float4(1.0)),
        Vertex3D(positions[2], normalize(positions[2]), float2(0.0, 0.5), float3(1.0, 0.0, 0.0), float3(0.0, 0.0, -1.0), float4(1.0)),
        Vertex3D(positions[5], normalize(positions[5]), float2(0.5, 1.0), float3(1.0, 0.0, 0.0), float3(0.0, 0.0, -1.0), float4(1.0))
    );

    // Calculate bounds
    mesh.bounds_min = float3(-radius);
    mesh.bounds_max = float3(radius);
    mesh.center = float3(0.0);
    mesh.radius = radius;

    return mesh;
}

// === MESH TRANSFORMATIONS ===

// Transform mesh by matrix
Mesh3D transformMesh(Mesh3D mesh, float4x4 transform) {
    Mesh3D result = mesh;

    for (int i = 0; i < mesh.triangle_count; i++) {
        for (int j = 0; j < 3; j++) {
            float4 pos = mul(transform, float4(mesh.triangles[i].vertices[j].position, 1.0));
            float4 normal = mul(transform, float4(mesh.triangles[i].vertices[j].normal, 0.0));

            result.triangles[i].vertices[j].position = pos.xyz;
            result.triangles[i].vertices[j].normal = normalize(normal.xyz);
        }

        // Recalculate triangle normal
        Triangle3D tri = result.triangles[i];
        float3 e1 = tri.vertices[1].position - tri.vertices[0].position;
        float3 e2 = tri.vertices[2].position - tri.vertices[0].position;
        tri.normal = normalize(cross(e1, e2));
        tri.area = length(cross(e1, e2)) * 0.5;
        result.triangles[i] = tri;
    }

    // Update bounds (simplified)
    result.center = mul(transform, float4(mesh.center, 1.0)).xyz;

    return result;
}

// === RAY-TRIANGLE INTERSECTION ===

// Möller-Trumbore ray-triangle intersection
bool rayTriangleIntersect(float3 rayOrigin, float3 rayDir, Triangle3D triangle,
                         out float t, out float u, out float v, out float3 normal) {
    const float EPSILON = 0.0000001;

    float3 vertex0 = triangle.vertices[0].position;
    float3 vertex1 = triangle.vertices[1].position;
    float3 vertex2 = triangle.vertices[2].position;

    float3 edge1 = vertex1 - vertex0;
    float3 edge2 = vertex2 - vertex0;

    float3 h = cross(rayDir, edge2);
    float a = dot(edge1, h);

    if (a > -EPSILON && a < EPSILON) return false;

    float f = 1.0 / a;
    float3 s = rayOrigin - vertex0;
    u = f * dot(s, h);

    if (u < 0.0 || u > 1.0) return false;

    float3 q = cross(s, edge1);
    v = f * dot(rayDir, q);

    if (v < 0.0 || u + v > 1.0) return false;

    t = f * dot(edge2, q);

    if (t > EPSILON) {
        normal = triangle.normal;
        return true;
    }

    return false;
}

// === MESH RAY CASTING ===

// Cast ray against mesh
bool rayMeshIntersect(float3 rayOrigin, float3 rayDir, Mesh3D mesh, out float hitT, out float3 hitNormal) {
    hitT = 1000000.0;
    bool hit = false;

    for (int i = 0; i < mesh.triangle_count; i++) {
        float t, u, v;
        float3 normal;

        if (rayTriangleIntersect(rayOrigin, rayDir, mesh.triangles[i], t, u, v, normal)) {
            if (t > 0.0 && t < hitT) {
                hitT = t;
                hitNormal = normal;
                hit = true;
            }
        }
    }

    return hit;
}

// === MESH RENDERING ===

// Render mesh with PBR shading
float3 renderMesh(Mesh3D mesh, float3 rayOrigin, float3 rayDir, float3 lightDir, float3 lightColor,
                 float3 albedo, float metallic, float roughness) {

    float hitT;
    float3 hitNormal;

    if (rayMeshIntersect(rayOrigin, rayDir, mesh, hitT, hitNormal)) {
        float3 hitPos = rayOrigin + rayDir * hitT;

        // PBR shading
        vec3 view_dir = -rayDir;

        // Half vector
        vec3 half_dir = normalize(view_dir + lightDir);

        // Dot products
        float NdotL = dotClamped(hitNormal, lightDir);
        float NdotV = dotClamped(hitNormal, view_dir);
        float NdotH = dotClamped(hitNormal, half_dir);
        float LdotH = dotClamped(lightDir, half_dir);

        // F0
        vec3 F0 = computeF0(albedo, metallic);

        // BRDF
        vec3 specular = CookTorrance(NdotH, NdotV, NdotL, LdotH, roughness, F0);
        vec3 diffuse = diffuseEnergyConservation(albedo, F0, metallic) / PI;

        vec3 color = (diffuse + specular) * lightColor * NdotL;

        // Add ambient
        color += albedo * 0.1;

        // Add rim lighting
        color += rimlight(hitNormal, view_dir, vec3(0.2), 2.0, 0.3);

        return color;
    }

    return float3(0.0); // No hit
}

// === INSTANCED RENDERING ===

// Render multiple mesh instances
float3 renderMeshInstances(MeshInstance3D instances[16], int instanceCount, Mesh3D baseMesh,
                          float3 rayOrigin, float3 rayDir, float3 lightDir, float3 lightColor) {

    float closestT = 1000000.0;
    MeshInstance3D closestInstance;

    // Find closest intersection
    for (int i = 0; i < instanceCount; i++) {
        if (!instances[i].visible) continue;

        // Transform ray to local space
        float4x4 invTransform = inverse(instances[i].transform);
        float3 localRayOrigin = mul(invTransform, float4(rayOrigin, 1.0)).xyz;
        float3 localRayDir = mul(invTransform, float4(rayDir, 0.0)).xyz;

        float hitT;
        float3 hitNormal;

        if (rayMeshIntersect(localRayOrigin, localRayDir, baseMesh, hitT, hitNormal)) {
            if (hitT > 0.0 && hitT < closestT) {
                closestT = hitT;
                closestInstance = instances[i];
            }
        }
    }

    if (closestT < 1000000.0) {
        // Transform hit point back to world space
        float3 localHitPos = mul(closestInstance.transform, float4(rayOrigin + rayDir * closestT, 1.0)).xyz;
        float3 localNormal = mul(closestInstance.transform, float4(hitNormal, 0.0)).xyz;

        // PBR shading with instance material
        vec3 view_dir = -rayDir;
        vec3 half_dir = normalize(view_dir + lightDir);

        float NdotL = dotClamped(localNormal, lightDir);
        float NdotV = dotClamped(localNormal, view_dir);
        float NdotH = dotClamped(localNormal, half_dir);
        float LdotH = dotClamped(lightDir, half_dir);

        vec3 F0 = computeF0(closestInstance.color, closestInstance.metallic);
        vec3 specular = CookTorrance(NdotH, NdotV, NdotL, LdotH, closestInstance.roughness, F0);
        vec3 diffuse = diffuseEnergyConservation(closestInstance.color, F0, closestInstance.metallic) / PI;

        vec3 color = (diffuse + specular) * lightColor * NdotL;
        color += closestInstance.color * 0.1; // Ambient
        color += rimlight(localNormal, view_dir, vec3(0.2), 2.0, 0.3);

        return color;
    }

    return float3(0.0);
}

// === UTILITY FUNCTIONS ===

// Calculate mesh bounds
void calculateMeshBounds(inout Mesh3D mesh) {
    if (mesh.triangle_count == 0) return;

    mesh.bounds_min = float3(1000000.0);
    mesh.bounds_max = float3(-1000000.0);

    for (int i = 0; i < mesh.triangle_count; i++) {
        for (int j = 0; j < 3; j++) {
            mesh.bounds_min = min(mesh.bounds_min, mesh.triangles[i].vertices[j].position);
            mesh.bounds_max = max(mesh.bounds_max, mesh.triangles[i].vertices[j].position);
        }
    }

    mesh.center = (mesh.bounds_min + mesh.bounds_max) * 0.5;
    mesh.radius = length(mesh.bounds_max - mesh.center);
}

// Check if ray intersects mesh bounding box (for optimization)
bool rayBoundsIntersect(float3 rayOrigin, float3 rayDir, float3 boundsMin, float3 boundsMax) {
    float3 invDir = 1.0 / rayDir;
    float3 tMin = (boundsMin - rayOrigin) * invDir;
    float3 tMax = (boundsMax - rayOrigin) * invDir;

    float3 t1 = min(tMin, tMax);
    float3 t2 = max(tMin, tMax);

    float tNear = max(max(t1.x, t1.y), t1.z);
    float tFar = min(min(t2.x, t2.y), t2.z);

    return tNear <= tFar && tFar >= 0.0;
}

#endif
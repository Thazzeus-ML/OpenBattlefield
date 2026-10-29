// Copyright OpenBattlefield contributors. Licensed under the Apache License, Version 2.0.
//
// STATUS: not yet compiled against Unreal Engine. See Docs/status.md.
#pragma once

#include "CoreMinimal.h"
#include "ProceduralMeshComponent.h"

#include "OpenBattlefieldCore/ObChunkMesher.h"

/** Buffers in the layout UProceduralMeshComponent::CreateMeshSection_LinearColor expects. */
struct FOBMeshBuffers
{
    TArray<FVector> Vertices;
    TArray<int32> Triangles;
    TArray<FVector> Normals;
    TArray<FVector2D> UV0;
    TArray<FLinearColor> VertexColors;
    TArray<FProcMeshTangent> Tangents;

    void Reset()
    {
        Vertices.Reset();
        Triangles.Reset();
        Normals.Reset();
        UV0.Reset();
        VertexColors.Reset();
        Tangents.Reset();
    }
};

/**
 * Converts a core mesh (chunk-local voxel units) to Unreal buffers in centimetres.
 * Material ids are written to the red vertex-colour channel as id / 255 for material graphs.
 * The core emits counter-clockwise faces for a right-handed frame; set bFlipWinding if your
 * engine version renders them inside out (see Docs/rendering-collision.md).
 */
void OBConvertChunkMesh(const ob::MeshData& Mesh, double VoxelSizeCm, bool bFlipWinding, FOBMeshBuffers& Out);

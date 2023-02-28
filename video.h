// c47edit - Scene editor for HM C47
// Copyright (C) 2018 AdrienTD
// Licensed under the GPL3+.
// See LICENSE file for more details.

#pragma once

struct Mesh;

extern int drawframes;
extern bool rendertextures;

void InitVideo();
void BeginDrawing();
void EndDrawing();
void BeginMeshDraw();
void DrawMesh(Mesh* mesh);
void InvalidateMesh(Mesh* mesh);

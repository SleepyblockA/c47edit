// c47edit - Scene editor for HM C47
// Copyright (C) 2018 AdrienTD
// Licensed under the GPL3+.
// See LICENSE file for more details.

#define _USE_MATH_DEFINES
#include <cmath>
#include <ctime>
#include <filesystem>
#include <functional>
#include <memory>

#include "chunk.h"
#include "classInfo.h"
#include "gameobj.h"
#include "global.h"
#include "texture.h"
#include "vecmat.h"
#include "video.h"
#include "window.h"
#include "ObjModel.h"
#include "GuiUtils.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <gl/GL.h>
#include <gl/GLU.h>
#include <commdlg.h>
#include "imgui/imgui.h"
#include "imgui/imgui_impl_opengl2.h"
#include "imgui/imgui_impl_win32.h"

GameObject *selobj = 0, *viewobj = 0;
float objviewscale = 0.0f;
Vector3 campos(0, 0, -50), camori(0,0,0);
float camNearDist = 1.0f, camFarDist = 10000.0f;
float camspeed = 32;
bool wireframe = false;
bool findsel = false;
uint32_t framesincursec = 0, framespersec = 0, lastfpscheck;
Vector3 cursorpos(0, 0, 0);
bool renderExc = false;

GameObject *bestpickobj = 0;
float bestpickdist;
Vector3 bestpickintersectionpnt(0, 0, 0);

extern HWND hWindow;

void ferr(const char *str)
{
	//printf("Error: %s\n", str);
	MessageBox(hWindow, str, "Fatal Error", 16);
	exit(-1);
}

void warn(const char *str)
{
	MessageBox(hWindow, str, "Warning", 48);
}

bool ObjInObj(GameObject *a, GameObject *b)
{
	GameObject *o = a;
	while (o = o->parent)
	{
		if (o == b)
			return true;
	}
	return false;
}

int IGStdStringInputCallback(ImGuiInputTextCallbackData* data) {
	if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
		std::string* str = (std::string*)data->UserData;
		str->resize(data->BufTextLen);
		data->Buf = (char*)str->data();
	}
	return 0;
}
bool IGStdStringInput(const char* label, std::string& str) {
	return ImGui::InputText(label, str.data(), str.capacity() + 1, ImGuiInputTextFlags_CallbackResize, IGStdStringInputCallback, &str);
}

void IGOTNode(GameObject *o)
{
	bool op, colorpushed = 0;
	if (o == superroot)
		ImGui::SetNextItemOpen(true, ImGuiCond_Once);
	if (findsel)
		if (ObjInObj(selobj, o))
			ImGui::SetNextItemOpen(true, ImGuiCond_Always);
	if (o == viewobj) {
		colorpushed = 1;
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0, 1, 0, 1));
	}
	op = ImGui::TreeNodeEx(o, (o->subobj.empty() ? ImGuiTreeNodeFlags_Leaf : 0) | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ((o == selobj) ? ImGuiTreeNodeFlags_Selected : 0), "%s(0x%X)::%s", ClassInfo::GetObjTypeString(o->type), o->type, o->name.c_str());
	if (colorpushed)
		ImGui::PopStyleColor();
	if (findsel)
		if (selobj == o)
			ImGui::SetScrollHereY();
	if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(0)) {
		ImGuiIO& io = ImGui::GetIO();
		if (io.KeyShift)
			viewobj = o;
		else
		{
			selobj = o;
			cursorpos = selobj->position;
		}
	}
	if (ImGui::IsItemActive())
		if (ImGui::BeginDragDropSource()) {
			ImGui::SetDragDropPayload("GameObject", &o, sizeof(GameObject*));
			ImGui::Text("GameObject: %s", o->name.c_str());
			ImGui::EndDragDropSource();
		}
	if(op)
	{
		for (auto e = o->subobj.begin(); e != o->subobj.end(); e++)
			IGOTNode(*e);
		ImGui::TreePop();
	}
}

void IGObjectTree()
{
	ImGui::SetNextWindowPos(ImVec2(3, 3), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(316, 652), ImGuiCond_FirstUseEver);
	ImGui::Begin("Object tree", 0, ImGuiWindowFlags_HorizontalScrollbar);
	IGOTNode(superroot);
	findsel = false;
	ImGui::End();
}

Vector3 GetYXZRotVecFromMatrix(Matrix *m)
{
	float b = atan2(m->_31, m->_33);
	float j = atan2(m->_12, m->_22);
	float a = asin(-m->_32);
	return Vector3(a, b, j);
}

#define swap_rb(a) ( (a & 0xFF00FF00) | ((a & 0xFF0000) >> 16) | ((a & 255) << 16) )

GameObject *objtogive = 0;
uint32_t curtexid = 0;

void IGObjectInfo()
{
	GameObject *nextobjtosel = 0;
	ImGui::SetNextWindowPos(ImVec2(1005, 3), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(270, 445), ImGuiCond_FirstUseEver);
	ImGui::Begin("Object information");
	if (!selobj)
		ImGui::Text("No object selected.");
	else {
		if (ImGui::Button("Duplicate"))
			if(selobj->root)
				DuplicateObject(selobj, selobj->root);
		ImGui::SameLine();
		bool wannadel = 0;
		if (ImGui::Button("Delete"))
			wannadel = 1;
		
		if (ImGui::Button("Set to be given"))
			objtogive = selobj;
		ImGui::SameLine();
		if (ImGui::Button("Give it here!"))
			if(objtogive)
				GiveObject(objtogive, selobj);
		
		if (ImGui::Button("Find in tree"))
			findsel = true;
		if (selobj->parent)
		{
			ImGui::SameLine();
			if (ImGui::Button("Select parent"))
				selobj = selobj->parent;
		}
		if (ImGui::Button("Import OBJ")) {
			auto filepath = GuiUtils::OpenDialogBox("Wavefront OBJ model\0*.OBJ\0\0\0", "obj");
			if (!filepath.empty()) {
				std::vector<Vector3> objVertices;
				std::vector<uint16_t> triIndices;
				std::vector<uint16_t> quadIndices;
				//std::vector<std::array<float, 8>> triUvs;
				//std::vector<std::array<float, 8>> quadUvs;

				ObjModel objmodel;
				objmodel.load(filepath);
				objVertices = std::move(objmodel.vertices);
				for (auto& vec : objVertices)
					vec *= Vector3(-1.0f, 1.0f, 1.0f);
				for (auto& tri : objmodel.triangles) {
					triIndices.insert(triIndices.end(), tri.begin(), tri.end());
				}

				selobj->flags |= 0x20;
				if (!selobj->mesh)
					selobj->mesh = new Mesh;
				else
					*selobj->mesh = {};

				Mesh* mesh = selobj->mesh;
				mesh->numverts = std::size(objVertices);
				mesh->numquads = std::size(quadIndices) / 4;
				mesh->numtris = std::size(triIndices) / 3;
				int faceIndex = pfac->maindata.size() / 2;
				mesh->vertstart = pver->maindata.size() / 4;
				mesh->tristart = (mesh->numtris > 0) ? faceIndex : 0;
				mesh->quadstart = (mesh->numquads > 0) ? faceIndex + 3 * mesh->numtris : 0;
				mesh->ftxo = pftx->maindata.size() + 1;

				Chunk::DataBuffer pver_newdata(pver->maindata.size() + mesh->numverts * 12);
				Chunk::DataBuffer pfac_newdata(pfac->maindata.size() + mesh->numquads * 8 + mesh->numtris * 6);
				Chunk::DataBuffer pftx_newdata(pftx->maindata.size() + 12 + (mesh->numquads + mesh->numtris) * 12);
				Chunk::DataBuffer puvc_newdata(puvc->maindata.size() + (mesh->numquads + mesh->numtris) * 4 * 8 * 2); // 8 floats per face, 2 sets
				memcpy(pver_newdata.data(), pver->maindata.data(), pver->maindata.size());
				memcpy(pfac_newdata.data(), pfac->maindata.data(), pfac->maindata.size());
				memcpy(pftx_newdata.data(), pftx->maindata.data(), pftx->maindata.size());
				memcpy(puvc_newdata.data(), puvc->maindata.data(), puvc->maindata.size());

				float* verts = (float*)pver_newdata.data() + mesh->vertstart;
				uint16_t* faces = (uint16_t*)pfac_newdata.data() + faceIndex;
				uint32_t* ftxHead = (uint32_t*)(pftx_newdata.data() + pftx->maindata.size());
				uint16_t* ftx = (uint16_t*)(ftxHead + 3);
				float* uv1 = (float*)(puvc_newdata.data() + puvc->maindata.size());
				float* uv2 = (float*)(puvc_newdata.data() + puvc->maindata.size() + (mesh->numquads + mesh->numtris) * 4 * 8);

				memcpy(verts, objVertices.data(), objVertices.size() * 12);
				for (uint16_t x : triIndices)
					*faces++ = 2 * x;
				for (uint16_t x : quadIndices)
					*faces++ = 2 * x;
				uint32_t numFaces = mesh->numquads + mesh->numtris;
				ftxHead[0] = uv1 - (float*)puvc_newdata.data();
				ftxHead[1] = uv2 - (float*)puvc_newdata.data();
				ftxHead[2] = numFaces;
				for (uint32_t f = 0; f < numFaces; ++f) {
					*ftx++ = 0x00A0; // 0: no texture, 0x20 textured, 0x80 lighting/shading
					*ftx++ = 0;
					*ftx++ = 0x0135;
					*ftx++ = 0xFFFF;
					*ftx++ = 0;
					*ftx++ = 0x0CF8;
					*uv1++ = 0.0f; *uv1++ = 0.0f;
					*uv1++ = 0.0f; *uv1++ = 1.0f;
					*uv1++ = 1.0f; *uv1++ = 1.0f;
					*uv1++ = 1.0f; *uv1++ = 0.0f;
					*uv2++ = 0.0f; *uv2++ = 0.0f;
					*uv2++ = 0.0f; *uv2++ = 1.0f;
					*uv2++ = 1.0f; *uv2++ = 1.0f;
					*uv2++ = 1.0f; *uv2++ = 0.0f;
				}

				pver->maindata = std::move(pver_newdata);
				pfac->maindata = std::move(pfac_newdata);
				pftx->maindata = std::move(pftx_newdata);
				puvc->maindata = std::move(puvc_newdata);
				InvalidateMesh(mesh);
			}
		}
		ImGui::Separator();

		IGStdStringInput("Name", selobj->name);
		ImGui::InputScalar("State", ImGuiDataType_U32, &selobj->state);
		ImGui::InputScalar("Type", ImGuiDataType_U32, &selobj->type);
		ImGui::InputScalar("Flags", ImGuiDataType_U32, &selobj->flags, nullptr, nullptr, "%04X", ImGuiInputTextFlags_CharsHexadecimal);
		ImGui::Separator();
		ImGui::InputScalar("PDBL offset", ImGuiDataType_U32, &selobj->pdbloff, 0, 0, "%08X", ImGuiInputTextFlags_CharsHexadecimal);
		ImGui::InputScalar("PEXC offset", ImGuiDataType_U32, &selobj->pexcoff, 0, 0, "%08X", ImGuiInputTextFlags_CharsHexadecimal);
		ImGui::DragFloat3("Position", &selobj->position.x);
		/*for (int i = 0; i < 3; i++) {
			ImGui::PushID(i);
			ImGui::DragFloat3((i==0) ? "Matrix" : "", selobj->matrix.m[i]);
			ImGui::PopID();
		}*/
		Vector3 rota = GetYXZRotVecFromMatrix(&selobj->matrix);
		rota *= 180.0f * (float)M_1_PI;
		if (ImGui::DragFloat3("Orientation", &rota.x))
		{
			rota *= (float)M_PI / 180.0f;
			Matrix my = Matrix::getRotationYMatrix(rota.y);
			Matrix mx = Matrix::getRotationXMatrix(rota.x);
			Matrix mz = Matrix::getRotationZMatrix(rota.z);
			selobj->matrix = mz * mx * my;
		}
		ImGui::Text("Num. references: %u", selobj->refcount);
		if (ImGui::CollapsingHeader("DBL"))
		{
			auto members = ClassInfo::GetMemberNames(selobj);
			size_t memberIndex = 0;
			ImGui::InputScalar("Flags", ImGuiDataType_U32, &selobj->dblflags);
			int i = 0;
			for (auto e = selobj->dbl.begin(); e != selobj->dbl.end(); e++)
			{
				ImGui::PushID(i++);
				ImGui::Text("%1X", e->flags >> 4);
				ImGui::SameLine();
				static const std::string oobMember = "OOB";
				const auto& name = (memberIndex < members.size()) ? members[memberIndex++] : oobMember;
				switch (e->type)
				{
				case 0:
					ImGui::Text("0"); break;
				case 1:
					ImGui::InputDouble(name.c_str(), &std::get<double>(e->value)); break;
				case 2:
					ImGui::InputFloat(name.c_str(), &std::get<float>(e->value)); break;
				case 3:
				case 0xA:
				case 0xB:
				case 0xC:
				{
					//ImGui::InputInt(DBLEntry::getTypeName(e->type), (int*)&std::get<uint32_t>(e->value)); break;
					ImGui::InputInt(name.c_str(), (int*)&std::get<uint32_t>(e->value)); break;
				}
				case 4:
				case 5:
				{
					auto& str = std::get<std::string>(e->value);
					//IGStdStringInput((e->type == 5) ? "Filename" : "String", str);
					IGStdStringInput(name.c_str(), str);
					break;
				}
				case 6:
					ImGui::Separator(); break;
				case 7: {
					auto& data = std::get<std::vector<uint8_t>>(e->value);
					ImGui::Text("Data (%X): %zu bytes", e->type, data.size());
					ImGui::SameLine();
					if (ImGui::SmallButton("Export")) {
						FILE* file;
						fopen_s(&file, "c47data.bin", "wb");
						fwrite(data.data(), data.size(), 1, file);
						fclose(file);
					}
					ImGui::SameLine();
					if (ImGui::SmallButton("Import")) {
						FILE* file;
						fopen_s(&file, "c47data.bin", "rb");
						fseek(file, 0, SEEK_END);
						auto len = ftell(file);
						fseek(file, 0, SEEK_SET);
						data.resize(len);
						fread(data.data(), data.size(), 1, file);
						fclose(file);
					}
					break;
				}
				case 8:
					if (auto& obj = std::get<GORef>(e->value); obj.valid()) {
						ImGui::LabelText(name.c_str(), "Object %s::%s", ClassInfo::GetObjTypeString(obj->type), obj->name.c_str());
						if (ImGui::IsItemClicked())
							nextobjtosel = obj.get();
					}
					else
						ImGui::LabelText(name.c_str(), "Object <Invalid>");
					if (ImGui::BeginDragDropTarget())
					{
						if (const ImGuiPayload *pl = ImGui::AcceptDragDropPayload("GameObject"))
						{
							e->value.emplace<GORef>(*(GameObject**)pl->Data);
						}
						ImGui::EndDragDropTarget();
					}
					break;
				case 9: {
					auto& vec = std::get<std::vector<GORef>>(e->value);
					if (ImGui::BeginListBox("##Objlist", ImVec2(0, 64))) {
						for (auto& obj : vec)
						{
							ImGui::Text("%s", obj->name.c_str());
							if (ImGui::IsItemClicked())
								nextobjtosel = obj.get();
							if (ImGui::BeginDragDropTarget())
							{
								if (const ImGuiPayload *pl = ImGui::AcceptDragDropPayload("GameObject"))
								{
									obj = *(GameObject**)pl->Data;
								}
								ImGui::EndDragDropTarget();
							}
						}
						ImGui::EndListBox();
						ImGui::SameLine();
						ImGui::Text("%s\n%zu objects", name.c_str(), vec.size());
					}
					break;
				}
				case 0x3F:
					ImGui::Text("End"); break;
				default:
					ImGui::Text("Unknown type %u", e->type); break;
				}
				ImGui::PopID();
			}
		}
		if (selobj->mesh && ImGui::CollapsingHeader("Mesh"))
		{
			//ImGui::Separator();
			//ImGui::Text("Mesh");
			ImVec4 c = ImGui::ColorConvertU32ToFloat4(swap_rb(selobj->color));
			if (ImGui::ColorEdit4("Color", &c.x, 0))
				selobj->color = swap_rb(ImGui::ColorConvertFloat4ToU32(c));
			ImGui::Text("Vertex start index: %u", selobj->mesh->vertstart);
			ImGui::Text("Quad start index:   %u", selobj->mesh->quadstart);
			ImGui::Text("Tri start index:    %u", selobj->mesh->tristart);
			ImGui::Text("Vertex count: %u", selobj->mesh->numverts);
			ImGui::Text("Quad count:   %u", selobj->mesh->numquads);
			ImGui::Text("Tri count:    %u", selobj->mesh->numtris);
			ImGui::Text("FTXO offset: 0x%X", selobj->mesh->ftxo);
		}
		if (selobj->mesh && selobj->mesh->ftxo && ImGui::CollapsingHeader("FTXO")) {
			// TODO: place this in "DebugUI.cpp"
			if (ImGui::Button("Change texture")) {
				uint8_t* ftxpnt = (uint8_t*)pftx->maindata.data() + selobj->mesh->ftxo - 1;
				uint16_t* ftxFace = (uint16_t*)(ftxpnt + 12);
				uint32_t numFaces = *(uint32_t*)(ftxpnt + 8);
				for (size_t i = 0; i < numFaces; ++i) {
					ftxFace[2] = curtexid;
					ftxFace += 6;
				}
				InvalidateMesh(selobj->mesh);
			}
			bool hasFtx = selobj->mesh->ftxo && !(selobj->mesh->ftxo & 0x80000000);
			uint8_t* ftxpnt = (uint8_t*)pftx->maindata.data() + selobj->mesh->ftxo - 1;
			float* uvCoords = (float*)puvc->maindata.data() + *(uint32_t*)(ftxpnt);
			uint16_t* ftxFace = (uint16_t*)(ftxpnt + 12);
			size_t numFaces = selobj->mesh->numquads + selobj->mesh->numtris;
			ImGui::Text("UV  offset 0x%08X", *(uint32_t*)(ftxpnt));
			ImGui::Text("UV2 offset 0x%08X", *(uint32_t*)(ftxpnt+4));
			ImGui::Text("Num Faces  0x%08X (0x%08X)", *(uint32_t*)(ftxpnt+8), numFaces);
			ImGui::Separator();
			for (size_t i = 0; i < numFaces; ++i) {
				ImGui::Text("%04X %04X %04X %04X %04X %04X", ftxFace[0], ftxFace[1], ftxFace[2], ftxFace[3], ftxFace[4], ftxFace[5]);
				ImGui::Text("  (%.2f, %.2f), (%.2f, %.2f), (%.2f, %.2f)", uvCoords[0], uvCoords[1], uvCoords[2], uvCoords[3], uvCoords[4], uvCoords[5]);
				ftxFace += 6;
				uvCoords += 8;
			}
		}
		if (selobj->light && ImGui::CollapsingHeader("Light"))
		{
			//ImGui::Separator();
			//ImGui::Text("Light");
			char s[] = "Param ?\0";
			for (int i = 0; i < 7; i++)
			{
				s[6] = '0' + i;
				ImGui::InputScalar(s, ImGuiDataType_U32, &selobj->light->param[i], 0, 0, "%08X", ImGuiInputTextFlags_CharsHexadecimal);
			}
		}
		if (wannadel)
		{
			if (selobj->refcount > 0)
				warn("It's not possible to remove an object that is referenced by other objects!");
			else {
				RemoveObject(selobj);
				selobj = 0;
			}
		}
	}
	ImGui::End();
	if (nextobjtosel) selobj = nextobjtosel;
}

void IGMain()
{
	ImGui::SetNextWindowPos(ImVec2(1005, 453), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(270, 203), ImGuiCond_FirstUseEver);
	ImGui::Begin("c47edit");
	ImGui::Text("c47edit - Version " APP_VERSION);
	if (ImGui::Button("Save Scene"))
	{
		char newfn[300]; newfn[299] = 0;
		_splitpath(lastspkfn.c_str(), 0, 0, newfn, 0);
		strcat(newfn, ".zip");
		char *s = strrchr(newfn, '@');
		if (s)
			s += 1;
		else
			s = newfn;

		auto zipPath = GuiUtils::SaveDialogBox("Scene ZIP archive\0*.zip\0\0\0", "zip", s, "Save Scene ZIP archive as...");
		if (!zipPath.empty())
			SaveSceneSPK(zipPath.string().c_str());
	}
	ImGui::SameLine();
	if(ImGui::Button("About..."))
		MessageBox(hWindow, "c47edit\nUnofficial scene editor for \"Hitman: Codename 47\"\n\n"
			"(C) 2018 AdrienTD\nLicensed under the GPL 3.\nSee LICENSE file for details.\n\n"
			"3rd party libraries used:\n- Dear ImGui (MIT license)\n- Miniz (MIT license)\nSee LICENSE_* files for copyright and licensing of these libraries.", "c47edit", 0);
	//ImGui::DragFloat("Scale", &objviewscale, 0.1f);
	ImGui::DragFloat("Cam speed", &camspeed, 0.1f);
	ImGui::DragFloat3("Cam pos", &campos.x, 1.0f);
	ImGui::DragFloat2("Cam ori", &camori.x, 0.1f);
	ImGui::DragFloat2("Cam dist", &camNearDist, 1.0f);
	ImGui::DragFloat3("Cursor pos", &cursorpos.x);
	ImGui::Checkbox("Wireframe", &wireframe);
	ImGui::SameLine();
	ImGui::Checkbox("Textured", &rendertextures);
	ImGui::SameLine();
	ImGui::Checkbox("EXC", &renderExc);
	ImGui::Text("FPS: %u", framespersec);
	ImGui::End();
}

void IGTest()
{
	//ImGui::Begin("Debug/Test");

//#include "unused/moredebug.inc"

	//ImGui::End();
}

void IGTextures()
{
	ImGui::SetNextWindowSize(ImVec2(512.0f, 350.0f), ImGuiCond_FirstUseEver);
	ImGui::Begin("Textures");

	if (ImGui::Button("Add")) {
		auto filepath = GuiUtils::OpenDialogBox("Image\0*.png;*.bmp;*.jpg;*.jpeg;*.gif\0\0\0\0", "png");
		if (!filepath.empty()) {
			AddTexture(filepath);
			GlifyTexture(&g_palPack.subchunks.back());
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Replace")) {
		auto [palchk, dxtchk] = FindTextureChunk(curtexid);
		if (palchk) {
			auto fpath = GuiUtils::OpenDialogBox("Image\0*.png;*.bmp;*.jpg;*.jpeg;*.gif\0\0\0\0", "png");
			if (!fpath.empty()) {
				uint32_t tid = *(uint32_t*)palchk->maindata.data();
				ImportTexture(fpath, *palchk, *dxtchk, tid);
				InvalidateTexture(tid);
			}
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Export")) {
		Chunk* palchk = FindTextureChunk(curtexid).first;
		if (palchk) {
			TexInfo* ti = (TexInfo*)palchk->maindata.data();
			auto fpath = GuiUtils::SaveDialogBox("PNG Image\0*.png\0\0\0\0", "png", ti->name);
			if (!fpath.empty()) {
				ExportTexture(palchk, fpath);
			}
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Export all")) {
		auto dirpath = GuiUtils::SelectFolderDialogBox("Export all the textures in PNG to:");
		if (!dirpath.empty()) {
			for (Chunk& chk : g_palPack.subchunks) {
				TexInfo* ti = (TexInfo*)chk.maindata.data();
				std::string name = ti->name;
				if (name.empty()) {
					char tbuf[20];
					sprintf_s(tbuf, "NoName%04X", ti->id);
					name = tbuf;
				}
				ExportTexture(&chk, dirpath / (name + ".png"));
			}
		}
	}

	if (ImGui::BeginTable("TextureColumnsa", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_NoHostExtendY | ImGuiTableFlags_NoHostExtendX, ImGui::GetContentRegionAvail())) {
		ImGui::TableSetupColumn("TexListCol", ImGuiTableColumnFlags_WidthFixed, 256.0f);
		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		ImGui::BeginChild("TextureList");
		for (Chunk& chk : g_palPack.subchunks) {
			TexInfo* ti = (TexInfo*)chk.maindata.data();
			ImGui::PushID(ti);
			static const float imgsize = ImGui::GetTextLineHeightWithSpacing() * 2.0f;
			if (ImGui::Selectable("##texture", curtexid == ti->id, 0, ImVec2(0.0f, imgsize)))
				curtexid = ti->id;
			if (ImGui::IsItemVisible()) {
				ImGui::SameLine();
				auto t = texmap.find(ti->id);
				ImGui::Image((t != texmap.end()) ? t->second : nullptr, ImVec2(imgsize, imgsize));
				ImGui::SameLine();
				ImGui::Text("%i: %s\n%i*%i", ti->id, ti->name, ti->width, ti->height);
			}
			ImGui::PopID();
		}
		ImGui::EndChild();
		ImGui::TableNextColumn();
		if (Chunk* palchk = FindTextureChunk(curtexid).first) {
			TexInfo* ti = (TexInfo*)palchk->maindata.data();
			ImGui::Text("ID: %i\nSize: %i*%i\nNum mipmaps: %i\nFlags: %08X\nUnknown: %08X\nName: %s", ti->id, ti->width, ti->height, ti->numMipmaps, ti->flags, ti->random, ti->name);
			ImGui::Image(texmap.at(curtexid), ImVec2(ti->width, ti->height));
		}
		ImGui::EndTable();
	}

	ImGui::End();
}

GameObject* FindObjectNamed(const char *name, GameObject *sup = rootobj)
{
	if (sup->name == name)
		return sup;
	else
		for (auto e = sup->subobj.begin(); e != sup->subobj.end(); e++) {
			GameObject *r = FindObjectNamed(name, *e);
			if (r) return r;
		}
	return 0;
}

void RenderObject(GameObject *o)
{
	glPushMatrix();
	glTranslatef(o->position.x, o->position.y, o->position.z);
	glMultMatrixf(o->matrix.v);
	if (o->mesh && (o->flags & 0x20)) {
		if (!rendertextures) {
			uint32_t clr = swap_rb(o->color);
			glColor4ubv((uint8_t*)&clr);
		}
		DrawMesh(o->mesh);
	}
	for (auto e = o->subobj.begin(); e != o->subobj.end(); e++)
		RenderObject(*e);
	glPopMatrix();
}

Vector3 finalintersectpnt = Vector3(0, 0, 0);

bool IsRayIntersectingFace(Vector3 *raystart, Vector3 *raydir, int startvertex, int startface, int numverts, Matrix *worldmtx)
{
	uint16_t *bfac = (uint16_t*)pfac->maindata.data() + startface;
	float *bver = (float*)pver->maindata.data() + startvertex;

	std::unique_ptr<Vector3[]> pnts = std::make_unique<Vector3[]>(numverts);
	for (int i = 0; i < 3; i++)
	{
		Vector3 v(bver[bfac[i] * 3 / 2], bver[bfac[i] * 3 / 2 + 1], bver[bfac[i] * 3 / 2 + 2]);
		pnts[i] = v.transform(*worldmtx);
	}

	std::unique_ptr<Vector3[]> edges = std::make_unique<Vector3[]>(numverts);
	for (int i = 0; i < 2; i++)
		edges[i] = pnts[i + 1] - pnts[i];

	Vector3 planenorm = edges[1].cross(edges[0]);
	float planeord = -planenorm.dot(pnts[0]);

	float planenorm_dot_raydir = planenorm.dot(*raydir);
	if (planenorm_dot_raydir >= 0) return false;

	float param = -(planenorm.dot(*raystart) + planeord) / planenorm_dot_raydir;
	if (param < 0) return false;

	Vector3 interpnt = *raystart + *raydir * param;

	for (int i = 3; i < numverts; i++)
	{
		Vector3 v(bver[bfac[i] * 3 / 2], bver[bfac[i] * 3 / 2 + 1], bver[bfac[i] * 3 / 2 + 2]);
		pnts[i] = v.transform(*worldmtx);
	}

	for (int i = 2; i < numverts - 1; i++)
		edges[i] = pnts[i + 1] - pnts[i];
	edges[numverts - 1] = pnts[0] - pnts[numverts - 1];

	// Check if plane/ray intersection point is inside face

	for (int i = 0; i < numverts; i++)
	{
		Vector3 edgenorm = -planenorm.cross(edges[i]);
		Vector3 ptoi = interpnt - pnts[i];
		if (edgenorm.dot(ptoi) < 0)
			return false;
	}

	finalintersectpnt = interpnt;
	return true;
}

GameObject *IsRayIntersectingObject(Vector3 *raystart, Vector3 *raydir, GameObject *o, Matrix *worldmtx)
{
	float d;
	Matrix objmtx = o->matrix;
	objmtx._41 = o->position.x;
	objmtx._42 = o->position.y;
	objmtx._43 = o->position.z;
	objmtx *= *worldmtx;
	if (o->mesh)
	{
		Mesh *m = o->mesh;
		for (int i = 0; i < m->numquads; i++)
			if (IsRayIntersectingFace(raystart, raydir, m->vertstart, m->quadstart + i*4, 4, &objmtx))
				if ((d = (finalintersectpnt - campos).sqlen2xz()) < bestpickdist)
				{
					bestpickdist = d;
					bestpickobj = o;
					bestpickintersectionpnt = finalintersectpnt;
				}
		for(int i = 0; i < m->numtris; i++)
			if (IsRayIntersectingFace(raystart, raydir, m->vertstart, m->tristart  + i*3, 3, &objmtx))
				if ((d = (finalintersectpnt - campos).sqlen2xz()) < bestpickdist)
				{
					bestpickdist = d;
					bestpickobj = o;
					bestpickintersectionpnt = finalintersectpnt;
				}
	}
	for (auto c = o->subobj.begin(); c != o->subobj.end(); c++)
		IsRayIntersectingObject(raystart, raydir, *c, &objmtx);
	return 0;
}

int main(int argc, char* argv[])
//int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrevInst, char *args, int winmode)
{
	//SetProcessDPIAware();

	try {
		ClassInfo::ReadClassInfo();
	}
	catch (const std::exception& ex) {
		std::string err = "Failed to read the file classes.json.\n\nBe sure the file classes.json is present in the same folder that contains the c47edit executable.\n\nReason:\n";
		err += ex.what();
		MessageBoxA(nullptr, err.c_str(), "c47edit", 16);
		exit(-2);
	}

	auto zipPath = GuiUtils::OpenDialogBox("Scene ZIP archive\0*.zip\0\0\0", "zip", "Select a Scene ZIP archive (containing Pack.SPK)");
	if (zipPath.empty())
		exit(-1);
	LoadSceneSPK(zipPath.string().c_str());

	bool appnoquit = true;
	InitWindow();

	ImGui::CreateContext(0);
	ImGui::GetStyle().WindowRounding = 7.0f;
	ImGui_ImplWin32_Init((void*)hWindow);
	ImGui_ImplOpenGL2_Init();
	lastfpscheck = GetTickCount();

	GlifyAllTextures();

	while (appnoquit = HandleWindow())
	{
		if (win_minimized)
			Sleep(100);
		else
		{
			Vector3 cd(0, 0, 1), ncd;
			Matrix m1 = Matrix::getRotationXMatrix(camori.x);
			Matrix m2 = Matrix::getRotationYMatrix(camori.y);
			Matrix crm = m1 * m2; // order?
			//CreateRotationYXZMatrix(&crm, camori.y, camori.x, 0);
			ncd = cd.transform(crm);
			Vector3 crabnn;
			crabnn = Vector3(0, 1, 0).cross(ncd);
			Vector3 crab = crabnn.normal();

			Vector3 cammove(0, 0, 0);
			ImGuiIO& io = ImGui::GetIO();
			if (!io.WantCaptureKeyboard)
			{
				if (ImGui::IsKeyDown(VK_LEFT))
					cammove -= crab;
				if (ImGui::IsKeyDown(VK_RIGHT))
					cammove += crab;
				if (ImGui::IsKeyDown(VK_UP))
					cammove += ncd;
				if (ImGui::IsKeyDown(VK_DOWN))
					cammove -= ncd;
				if (ImGui::IsKeyDown('E'))
					cammove.y += 1;
				if (ImGui::IsKeyDown('D'))
					cammove.y -= 1;
				if (ImGui::IsKeyPressed('W'))
					wireframe = !wireframe;
				if (ImGui::IsKeyPressed('T'))
					rendertextures = !rendertextures;
			}
			campos += cammove * camspeed * (io.KeyShift ? 2 : 1);
			if (io.MouseDown[0] && !io.WantCaptureMouse && !(io.KeyAlt || io.KeyCtrl))
			{
				camori.y += io.MouseDelta.x * 0.01f;
				camori.x += io.MouseDelta.y * 0.01f;
			}
			if (!io.WantCaptureMouse && viewobj)
			if (io.MouseClicked[1] || (io.MouseClicked[0] && (io.KeyAlt || io.KeyCtrl)))
			{
				Vector3 raystart, raydir;
				float ys = 1 / tan(60.0f * (float)M_PI / 180.0f / 2.0f);
				float xs = ys / ((float)screen_width / (float)screen_height);
				ImVec2 mspos = ImGui::GetMousePos();
				float msx = mspos.x * 2.0f / (float)screen_width - 1.0f;
				float msy = mspos.y * 2.0f / (float)screen_height - 1.0f;
				Vector3 hi = ncd.cross(crab);
				raystart = campos + ncd + crab * (msx / xs) - hi * (msy / ys);
				raydir = raystart - campos;

				bestpickobj = 0;
				bestpickdist = std::numeric_limits<float>::infinity();
				Matrix mtx = Matrix::getIdentity();
				IsRayIntersectingObject(&raystart, &raydir, viewobj, &mtx);
				if (io.KeyAlt) {
					if (bestpickobj && selobj)
						selobj->position = bestpickintersectionpnt;
				}
				else
					selobj = bestpickobj;
				cursorpos = bestpickintersectionpnt;
			}

			ImGui_ImplOpenGL2_NewFrame();
			ImGui_ImplWin32_NewFrame();
			ImGui::NewFrame();
			IGMain();
			IGObjectTree();
			IGObjectInfo();
//#if 1
			IGTest();
//#endif
			IGTextures();
			ImGui::EndFrame();

			BeginDrawing();
			glClearColor(0.5f, 0.0f, 0.0f, 1.0f);
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

			glEnable(GL_DEPTH_TEST);
			glDepthFunc(GL_LEQUAL);
			glClearDepth(1.0f);
			glMatrixMode(GL_PROJECTION);
			glLoadIdentity();
			Matrix persp = Matrix::getLHPerspectiveMatrix(60.0f * (float)M_PI / 180.0f, (float)screen_width / (float)screen_height, camNearDist, camFarDist);
			glMultMatrixf(persp.v);
			Matrix lookat = Matrix::getLHLookAtViewMatrix(campos, campos + ncd, Vector3(0.0f, 1.0f, 0.0f));
			glMultMatrixf(lookat.v);
			glMatrixMode(GL_MODELVIEW);
			glLoadIdentity();
			float ovs = pow(2, objviewscale);
			glScalef(ovs, ovs, ovs);

			glEnable(GL_CULL_FACE);
			glCullFace(GL_BACK);
			glPolygonMode(GL_FRONT_AND_BACK, wireframe ? GL_LINE : GL_FILL);
			BeginMeshDraw();
			if (viewobj) {
				//glTranslatef(-viewobj->position.x, -viewobj->position.y, -viewobj->position.z);
				RenderObject(viewobj);
			}

			if (renderExc && viewobj) {
				if (Chunk* pexc = spkchk->findSubchunk('CXEP')) {
					glPointSize(5.0f);
					glBegin(GL_POINTS);
					auto renderAnim = [pexc](auto rec, GameObject* obj, const Matrix& prevmat) -> void {
						Matrix mat = obj->matrix * Matrix::getTranslationMatrix(obj->position) * prevmat;
						if (obj->pexcoff) {
							uint8_t* excptr = (uint8_t*)pexc->maindata.data() + obj->pexcoff - 1;
							Chunk exchk;
							exchk.load(excptr);
							assert(exchk.tag == 'HEAD');
							if (auto* keys = exchk.findSubchunk('KEYS')) {
								uint32_t cnt = *(uint32_t*)(keys->multidata[0].data());
								for (uint32_t i = 1; i <= cnt; ++i) {
									float* kpos = (float*)((char*)(keys->multidata[i].data()) + 4);
									Vector3 vpos = Vector3(kpos[0], kpos[1], kpos[2]).transform(mat);
									glVertex3fv(&vpos.x);
								}
							}
						}
						for (auto& child : obj->subobj) {
							rec(rec, child, mat);
						}
					};
					renderAnim(renderAnim, viewobj, Matrix::getIdentity());
					glEnd();
					glPointSize(1.0f);
				}
			}

			glPointSize(10);
			glColor3f(1, 1, 1);
			glBegin(GL_POINTS);
			glVertex3f(cursorpos.x, cursorpos.y, cursorpos.z);
			glEnd();
			glPointSize(1);

			ImGui::Render();
			ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
			EndDrawing();
			//_sleep(16);
			
			framesincursec++;
			uint32_t newtime = GetTickCount();
			if ((uint32_t)(newtime - lastfpscheck) >= 1000) {
				framespersec = framesincursec;
				framesincursec = 0;
				lastfpscheck = newtime;
			}
		}
	}
}
/*
* Wrapper layer to provide a DLL interface for Nifly.

  Data passed through simple buffers to prevent problems calling from different languages.
  
  Copyright (c) 2021, by Bad Dog
*/

/*
    TODO: Refactor so this whole interface is not dependent on a single reference skeleton.

    TODO: Walk through and make sure all the memory allocated here gets released
    */
#include "pch.h" // use stdafx.h in Visual Studio 2017 and earlier
#include <iostream>
#include <filesystem>
#include <string>
#include <algorithm>
#include "niffile.hpp"
#include "bhk.hpp"
#include "NiflyFunctions.hpp"
#include "NiflyWrapper.hpp"

const int NiflyDDLVersion[3] = { 5, 10, 0 };
 
using namespace nifly;

/* ************************** UTILITY ************************** */

void XformToBuffer(float* xform, MatTransform& tmp) {
    int i = 0;
    xform[i++] = tmp.translation.x;
    xform[i++] = tmp.translation.y;
    xform[i++] = tmp.translation.z;
    xform[i++] = tmp.rotation[0][0];
    xform[i++] = tmp.rotation[0][1];
    xform[i++] = tmp.rotation[0][2];
    xform[i++] = tmp.rotation[1][0];
    xform[i++] = tmp.rotation[1][1];
    xform[i++] = tmp.rotation[1][2];
    xform[i++] = tmp.rotation[2][0];
    xform[i++] = tmp.rotation[2][1];
    xform[i++] = tmp.rotation[2][2];
    xform[i++] = tmp.scale;
}


/* ******************* NIF FILE MANAGEMENT ********************* */

enum TargetGame StrToTargetGame(const char* gameName) {
    if (strcmp(gameName, "FO3") == 0) { return TargetGame::FO3; }
    else if (strcmp(gameName, "FONV") == 0) { return TargetGame::FONV; }
    else if (strcmp(gameName, "SKYRIM") == 0) { return TargetGame::SKYRIM; }
    else if (strcmp(gameName, "FO4") == 0) { return TargetGame::FO4; }
    else if (strcmp(gameName, "FO4VR") == 0) { return TargetGame::FO4VR; }
    else if (strcmp(gameName, "SKYRIMSE") == 0) { return TargetGame::SKYRIMSE; }
    else if (strcmp(gameName, "SKYRIMVR") == 0) { return TargetGame::SKYRIMVR; }
    else if (strcmp(gameName, "FO76") == 0) { return TargetGame::FO76; }
    else { return TargetGame::SKYRIM; }
}

NIFLY_API void* load(const char8_t* filename) {
    NifFile* nif = new NifFile();
    int errval = nif->Load(std::filesystem::path(filename));

    if (errval == 0) return nif;

    if (errval == 1) niflydll::LogWrite("File does not exist or is not a nif");
    if (errval == 2) niflydll::LogWrite("File is not a nif format we can read");

    return nullptr;
}

NIFLY_API void* getRoot(void* f) {
    NifFile* theNif = static_cast<NifFile*>(f);
    return theNif->GetRootNode();
}

NIFLY_API int getRootName(void* f, char* buf, int len) {
    NifFile* theNif = static_cast<NifFile*>(f);
    nifly::NiNode* root = theNif->GetRootNode();
    std::string name = root->name.get();
    int copylen = std::min((int)len - 1, (int)name.length());
    name.copy(buf, copylen, 0);
    buf[copylen] = '\0';
    return int(name.length());
}

NIFLY_API int getGameName(void* f, char* buf, int len) {
    NifFile* theNif = static_cast<NifFile*>(f);
    NiHeader hdr = theNif->GetHeader();
    NiVersion vers = hdr.GetVersion();
    std::string name = "";
    if (vers.IsFO3()) { name = "FO3"; }
    else if (vers.IsSK()) { name = "SKYRIM"; }
    else if (vers.IsSSE()) { name = "SKYRIMSE"; }
    else if (vers.IsFO4()) { name = "FO4"; }
    else if (vers.IsFO76()) { name = "FO76"; }

    int copylen = std::min((int)len - 1, (int)name.length());
    name.copy(buf, copylen, 0);
    buf[copylen] = '\0';
    return int(name.length());
}

NIFLY_API const int* getVersion() {
    return NiflyDDLVersion;
};

NIFLY_API void* nifCreate() {
    return new NifFile;
}

NIFLY_API void destroy(void* f) {
    NifFile* theNif = static_cast<NifFile*>(f);
    theNif->Clear();
    delete theNif;
}

void SetNifVersionWrap(NifFile* nif, enum TargetGame targ, int rootType, std::string name) {
    NiVersion version;

    switch (targ) {
    case FO3:
    case FONV:
        version.SetFile(V20_2_0_7);
        version.SetUser(11);
        version.SetStream(34);
        break;
    case SKYRIM:
        version.SetFile(V20_2_0_7);
        version.SetUser(12);
        version.SetStream(83);
        break;
    case FO4:
    case FO4VR:
        version.SetFile(V20_2_0_7);
        version.SetUser(12);
        version.SetStream(130);
        break;
    case SKYRIMSE:
    case SKYRIMVR:
        version.SetFile(V20_2_0_7);
        version.SetUser(12);
        version.SetStream(100);
        break;
    case FO76:
        version.SetFile(V20_2_0_7);
        version.SetUser(12);
        version.SetStream(155);
        break;
    }

    if (rootType == RT_BSFADENODE)
        nif->CreateAsFade(version, name);
    else
        nif->Create(version);
    //NiNode* root = nif->GetRootNode();
    //std::string nm = root->GetName();
    //root->SetName(name);
}

NIFLY_API void* createNif(const char* targetGameName, int rootType, const char* rootName) {
    TargetGame targetGame = StrToTargetGame(targetGameName);
    NifFile* workNif = new NifFile();
    std::string rootNameStr = rootName;
    SetNifVersionWrap(workNif, targetGame, rootType, rootNameStr);
    return workNif;
}

NIFLY_API int saveNif(void* the_nif, const char8_t* filename) {
    NifFile* nif = static_cast<NifFile*>(the_nif);
    return nif->Save(std::filesystem::path(filename));
}


/* ********************* NODE HANDLING ********************* */

NIFLY_API int getNodeCount(void* theNif)
{
    NifFile* nif = static_cast<NifFile*>(theNif);
    return int(nif->GetNodes().size());
}

NIFLY_API void getNodes(void* theNif, void** buf)
{
    NifFile* nif = static_cast<NifFile*>(theNif);
    std::vector<nifly::NiNode*> nodes = nif->GetNodes();
    for (int i = 0; i < nodes.size(); i++)
        buf[i] = nodes[i];
}

NIFLY_API int getNodeBlockname(void* node, char* buf, int buflen) {
    nifly::NiNode* theNode = static_cast<nifly::NiNode*>(node);
    std::string name = theNode->GetBlockName();
    int copylen = std::min((int)buflen - 1, (int)name.length());
    name.copy(buf, copylen, 0);
    buf[name.length()] = '\0';
    return int(name.length());
}

NIFLY_API int getNodeFlags(void* node) {
    nifly::NiNode* theNode = static_cast<nifly::NiNode*>(node);
    return theNode->flags;
}

NIFLY_API void setNodeFlags(void* node, int theFlags) {
    nifly::NiNode* theNode = static_cast<nifly::NiNode*>(node);
    theNode->flags = theFlags;
}

NIFLY_API int getNodeName(void* node, char* buf, int buflen) {
    nifly::NiNode* theNode = static_cast<nifly::NiNode*>(node);
    std::string name = theNode->name.get();
    int copylen = std::min((int)buflen - 1, (int)name.length());
    name.copy(buf, copylen, 0);
    buf[name.length()] = '\0';
    return int(name.length());
}

NIFLY_API void* getNodeParent(void* theNif, void* node) {
    NifFile* nif = static_cast<NifFile*>(theNif);
    nifly::NiNode* theNode = static_cast<nifly::NiNode*>(node);
    return nif->GetParentNode(theNode);
}

NIFLY_API void* addNode(void* f, const char* name, const MatTransform* xf, void* parent) {
    NifFile* nif = static_cast<NifFile*>(f);
    NiNode* parentNode = static_cast<NiNode*>(parent);
    NiNode* theNode = nif->AddNode(name, *xf, parentNode);
    return theNode;
}


/* ********************* SHAPE MANAGEMENT ********************** */

int NIFLY_API getAllShapeNames(void* f, char* buf, int len) {
    NifFile* theNif = static_cast<NifFile*>(f);
    std::vector<std::string> names = theNif->GetShapeNames();
    std::string s = "";
    for (auto& sn : names) {
        if (s.length() > 0) s += "\n";
        s += sn;
    }
    int copylen = std::min((int)len - 1, (int)s.length());
    s.copy(buf, copylen, 0);
    buf[copylen] = '\0';

    return int(names.size());
}

NIFLY_API int getShapeName(void* theShape, char* buf, int len) {
    nifly::NiShape* shape = static_cast<nifly::NiShape*>(theShape);
    std::string name = shape->name.get();
    int copylen = std::min((int)len - 1, (int)name.length());
    name.copy(buf, copylen, 0);
    buf[copylen] = '\0';
    return int(name.length());
}

int NIFLY_API loadShapeNames(const char* filename, char* buf, int len) {
    NifFile* theNif = new NifFile(std::filesystem::path(filename));
    std::vector<std::string> names = theNif->GetShapeNames();
    std::string s = "";
    for (auto& sn : names) {
        if (s.length() > 0) s += "\n";
        s += sn;
    }
    int copylen = std::min((int)len - 1, (int)s.length());
    s.copy(buf, copylen, 0);
    buf[copylen] = '\0';

    theNif->Clear();
    delete theNif;
    return int(names.size());
}

int NIFLY_API getShapes(void* f, void** buf, int len, int start) {
    NifFile* theNif = static_cast<NifFile*>(f);
    std::vector<nifly::NiShape*> shapes = theNif->GetShapes();
    for (int i=start, j=0; (j < len) && (i < shapes.size()); i++)
        buf[j++] = shapes[i];
    return int(shapes.size());
}

NIFLY_API int getShapeBlockName(void* theShape, char* buf, int buflen) {
    NiShape* shape = static_cast<nifly::NiShape*>(theShape);
    const char* blockname = shape->GetBlockName();
    strncpy_s(buf, buflen, blockname, buflen);
    //strncpy(buf, blockname, buflen);
    buf[buflen - 1] = '\0';
    return int(strlen(blockname));
}

NIFLY_API int getVertsForShape(void* theNif, void* theShape, float* buf, int len, int start)
/*
    Get a shape's verts.
    buf, len = buffer that receives triples. len is length of buffer in floats.
    start = vertex index to start with.
    */
{
    NifFile* nif = static_cast<NifFile*>(theNif);
    nifly::NiShape* shape = static_cast<nifly::NiShape*>(theShape);
    std::vector<nifly::Vector3> verts;
    nif->GetVertsForShape(shape, verts);
    for (int i = start, j = 0; j < len && i < verts.size(); i++) {
        buf[j++] = verts.at(i).x;
        buf[j++] = verts.at(i).y;
        buf[j++] = verts.at(i).z;
    }
    return int(verts.size());
}

NIFLY_API int getNormalsForShape(void* theNif, void* theShape, float* buf, int len, int start)
/*
    Get a shape's normals.
    buf, len = buffer that receives triples. len is length of buffer in floats.
    start = normal index to start with.
    */
{
    NifFile* nif = static_cast<NifFile*>(theNif);
    nifly::NiShape* shape = static_cast<nifly::NiShape*>(theShape);
    const std::vector<nifly::Vector3>* norms;
    norms = nif->GetNormalsForShape(shape);
    if (norms) {
        for (int i = start, j = 0; j < len && i < norms->size(); i++) {
            buf[j++] = norms->at(i).x;
            buf[j++] = norms->at(i).y;
            buf[j++] = norms->at(i).z;
        }
        return int(norms->size());
    }
    else
        return 0;
}
//NIFLY_API int getRawVertsForShape(void* theNif, void* theShape, float* buf, int len, int start)
//{
//    NifFile* nif = static_cast<NifFile*>(theNif);
//    nifly::NiShape* shape = static_cast<nifly::NiShape*>(theShape);
//    const std::vector<nifly::Vector3>* verts = nif->GetRawVertsForShape(shape);
//    for (int i = start, j = 0; i < start + len && i < verts->size(); i++) {
//        buf[j++] = verts->at(i).x;
//        buf[j++] = verts->at(i).y;
//        buf[j++] = verts->at(i).z;
//    }
//    return verts->size();
//}
NIFLY_API int getTriangles(void* theNif, void* theShape, uint16_t* buf, int len, int start)
/*
    Get a shape's tris.
    buf, len = buffer that receives triples. len is length of buffer in uint16's.
    start = tri index to start with.
    */
{
    NifFile* nif = static_cast<NifFile*>(theNif);
    nifly::NiShape* shape = static_cast<nifly::NiShape*>(theShape);
    std::vector<nifly::Triangle> shapeTris;
    shape->GetTriangles(shapeTris);
    for (int i=start, j=0; j < len && i < shapeTris.size(); i++) {
        buf[j++] = shapeTris.at(i).p1;
        buf[j++] = shapeTris.at(i).p2;
        buf[j++] = shapeTris.at(i).p3;
    }
    return int(shapeTris.size());
}

NIFLY_API int getUVs(void* theNif, void* theShape, float* buf, int len, int start)
{
    NifFile* nif = static_cast<NifFile*>(theNif);
    nifly::NiShape* shape = static_cast<nifly::NiShape*>(theShape);
    const std::vector<nifly::Vector2>* uv = nif->GetUvsForShape(shape);
    for (int i = start, j = 0; j < len && i < uv->size(); i++) {
        buf[j++] = uv->at(i).u;
        buf[j++] = uv->at(i).v;
    }
    return int(uv->size());
}

NIFLY_API void* createNifShapeFromData(void* parentNif,
    const char* shapeName,
    const float* verts,
    const float* uv_points,
    const float* norms,
    int vertCount,
    const uint16_t* tris, int triCount,
    uint16_t* optionsPtr,
    void* parentRef)
    /* Create nif shape from the given data
    * verts = (float x, float y float z), ... 
    * uv_points = (float u, float v), matching 1-1 with the verts list
    * norms = (float, float, float) matching 1-1 with the verts list. May be null.
    * vertCount = number of verts in verts list (and uv pairs and normals in those lists)
    * tris = (uint16, uiint16, uint16) indices into the vertex list
    * triCount = # of tris in the tris list (buffer is 3x as long)
    * optionsPtr == 1: Create SSE head part (so use BSDynamicTriShape)
    *            == 2: Create FO4 BSTriShape (default is BSSubindexTriShape)
    *            == 4: Create FO4 BSEffectShaderProperty
    *            may be omitted
    * parentRef = Node to be parent of the new shape. Root if omitted.
    */
{
    NifFile* nif = static_cast<NifFile*>(parentNif);
    std::vector<Vector3> v;
    std::vector<Triangle> t;
    std::vector<Vector2> uv;
    std::vector<Vector3> n;

    for (int i = 0; i < vertCount; i++) {
        Vector3 thisv;
        thisv[0] = verts[i*3];
        thisv[1] = verts[i*3 + 1];
        thisv[2] = verts[i*3 + 2];
        v.push_back(thisv);

        Vector2 thisuv;
        thisuv.u = uv_points[i*2];
        thisuv.v = uv_points[i*2+1];
        uv.push_back(thisuv);

        if (norms) {
            Vector3 thisnorm;
            thisnorm[0] = norms[i*3];
            thisnorm[1] = norms[i*3+1];
            thisnorm[2] = norms[i*3+2];
            n.push_back(thisnorm);
        };
    }
    for (int i = 0; i < triCount; i++) {
        Triangle thist;
        thist[0] = tris[i*3];
        thist[1] = tris[i*3+1];
        thist[2] = tris[i*3+2];
        t.push_back(thist);
    }

    uint16_t opt = 0;
    if (optionsPtr) opt = *optionsPtr;
    NiNode* parent = nullptr;
    if (parentRef) parent = static_cast<NiNode*>(parentRef);

    return PyniflyCreateShapeFromData(nif, shapeName, 
            &v, &t, &uv, &n, opt, parent);
}


/* ********************* TRANSFORMS AND SKINNING ********************* */

NIFLY_API void* makeGameSkeletonInstance(const char* gameName) {
    return MakeSkeleton(StrToTargetGame(gameName));
};

NIFLY_API void* makeSkeletonInstance(const char* skelPath, const char* rootName) {
    AnimSkeleton* skel = AnimSkeleton::MakeInstance();
    skel->LoadFromNif(skelPath, rootName);
    return skel;
}

NIFLY_API void* loadSkinForNif(void* nifRef, const char* game)
/* Return a AnimInfo based on the given nif and shape. This saves time because it only
    needs to be loaded once.
    Parameters:
        NifFile* - nif to load
        game - name of the game to use for skeleton
    Returns
        AnimInfo* - AnimInfo loaded with all shapes in the nif
    */
{
    AnimSkeleton* skel = AnimSkeleton::MakeInstance();
    std::string root;
    std::string fn = SkeletonFile(StrToTargetGame(game), root);
    skel->LoadFromNif(fn, root);

    AnimInfo* skin = new AnimInfo();
    skin->SetSkeleton(skel);
    skin->LoadFromNif(static_cast<NifFile*>(nifRef), skel);
    return skin;
}

NIFLY_API void* loadSkinForNifSkel(void* nifRef, void* skel)
/* Return a AnimInfo based on the given nif and shape. This saves time because it only
    needs to be loaded once.
    Parameters:
        NifFile* - nif to load
        skel - AnimSkeleton to use
    Returns
        AnimInfo* - AnimInfo loaded with all shapes in the nif
    */
{

    AnimInfo* skin = new AnimInfo();
    skin->SetSkeleton(static_cast<AnimSkeleton*>(skel));
    skin->LoadFromNif(static_cast<NifFile*>(nifRef), 
                      static_cast<AnimSkeleton*>(skel));
    return skin;
}

NIFLY_API bool getShapeGlobalToSkin(void* nifRef, void* shapeRef, float* xform) {
    NifFile* nif = static_cast<NifFile*>(nifRef);
    MatTransform tmp;
    bool skinInstFound = nif->GetShapeTransformGlobalToSkin(static_cast<NiShape*>(shapeRef), tmp);
    if (skinInstFound) XformToBuffer(xform, tmp);
    return skinInstFound;
}

NIFLY_API void getGlobalToSkin(void* nifSkinRef, void* shapeRef, void* xform) 
/* Return the global-to-skin transform for the given shape 
*   Parameters
*   > AnimInfo* nifSkinRef = AnimInfo* for the nif
*   > NIShape* shapeRef = shape to get the transform for
*   < MatTransform* xform = buffer to hold the transform
*/
{
    GetGlobalToSkin(static_cast<AnimInfo*>(nifSkinRef), 
                    static_cast<NiShape*>(shapeRef), 
                    static_cast<MatTransform*>(xform));
}

NIFLY_API int hasSkinInstance(void* shapeRef) {
    return static_cast<NiShape*>(shapeRef)->HasSkinInstance()? 1: 0;
}

NIFLY_API bool getShapeSkinToBone(void* nifPtr, void* shapePtr, const  char* boneName, float* buf) {
    MatTransform xf;
    bool hasXform = static_cast<NifFile*>(nifPtr)->GetShapeTransformSkinToBone(
        static_cast<NiShape*>(shapePtr),
        std::string(boneName),
        xf);
    if (hasXform) XformToBuffer(buf, xf);
    return hasXform;
}

NIFLY_API void getTransform(void* theShape, float* buf) {
    nifly::NiShape* shape = static_cast<nifly::NiShape*>(theShape);
    nifly::MatTransform xf = shape->GetTransformToParent();
    XformToBuffer(buf, xf);
}

NIFLY_API void getNodeTransform(void* theNode, MatTransform* buf) {
    nifly::NiNode* node = static_cast<nifly::NiNode*>(theNode);
    *buf = node->GetTransformToParent();
}

NIFLY_API void getNodeXformToGlobal(
    void* anim, const char* boneName, MatTransform* xformBuf) {
    /* Get the transform from the nif if there, from the reference skeleton if not.
        Requires an AnimInfo because this is a skinned nif, after all. Creating the 
        AnimInfo loads the skeleton.
        > AnimInfo* anim - The nif's AnimInfo
        > char* boneName - name of the bone
        < MatTransform* xformBuf - Buffer to receive the transform
        */
    AnimInfo* nifskin = static_cast<AnimInfo*>(anim);
    NifFile* nif = nifskin->GetRefNif();
    static const MatTransform matEmpty;

    *xformBuf = matEmpty;
    if (! nif->GetNodeTransformToGlobal(boneName, *xformBuf)) {
        AnimSkeleton* skel = nifskin->GetSkeleton(); // need ref skeleton here?
        AnimBone* thisBone = skel->GetBonePtr(boneName);
        if (thisBone) {
            *xformBuf = thisBone->xformToGlobal;
        }
    }
}

NIFLY_API void getBoneSkinToBoneXform(void* nifSkinPtr, const char* shapeName,
    const char* boneName, float* xform) {
    AnimInfo* anim = static_cast<AnimInfo*>(nifSkinPtr);
    int boneIdx = anim->GetShapeBoneIndex(shapeName, boneName);
    AnimSkin* skin = &anim->shapeSkinning[boneName];
    XformToBuffer(xform, skin->boneWeights[boneIdx].xformSkinToBone);
}

NIFLY_API void* createSkinForNif(void* nifPtr, const char* gameName) 
/* Create a skin (AnimInfo) for the nif.
*  NOTE THIS WILL RELOAD THE REFERENCE SKELETON
*   Parameters
*   > NifFIle* nifPtr
*   > char* gameName 
*   < returns AnimInfo* 
*/
{
    NifFile* nif = static_cast<NifFile*>(nifPtr);
    return CreateSkinForNif(nif, StrToTargetGame(gameName));
}

NIFLY_API void skinShape(void* nif, void* shapeRef)
{
    static_cast<NifFile*>(nif)->CreateSkinning(static_cast<nifly::NiShape*>(shapeRef));
}

NIFLY_API void writeSkinToNif(void* animref) {
    /* Write skin info to nif, creating bone nodes as needed
    */
    AnimInfo* anim = static_cast<AnimInfo*>(animref);
    NifFile* theNif = anim->GetRefNif();
    anim->WriteToNif(theNif, "None");
    for (auto& shape : theNif->GetShapes())
        theNif->UpdateSkinPartitions(shape);
}

NIFLY_API int saveSkinnedNif(void* animref, const char8_t* filepath) {
    /* Save skinned nif
    *   Parameters
    *   > AnimInfo* anim = Nif skin for the nif to save
    *   > char* filepath
    */
    AnimInfo* anim = static_cast<AnimInfo*>(animref);
    writeSkinToNif(animref);
    return saveNif(anim->GetRefNif(), filepath);
    //return SaveSkinnedNif(static_cast<AnimInfo*>(anim), std::filesystem::path(filepath));
}

NIFLY_API void setGlobalToSkinXform(void* animPtr, void* shapePtr, void* gtsXformPtr) {
    if (static_cast<NiShape*>(shapePtr)->HasSkinInstance()) {
        SetShapeGlobalToSkinXform(static_cast<AnimInfo*>(animPtr),
            static_cast<NiShape*>(shapePtr),
            *static_cast<MatTransform*>(gtsXformPtr));
    }
    else {
        SetGlobalToSkinXform(
            static_cast<AnimInfo*>(animPtr),
            static_cast<NiShape*>(shapePtr),
            *static_cast<MatTransform*>(gtsXformPtr));
    }
}

NIFLY_API void setShapeGlobalToSkinXform(void* animPtr, void* shapePtr, void* gtsXformPtr) {
    SetShapeGlobalToSkinXform(static_cast<AnimInfo*>(animPtr),
        static_cast<NiShape*>(shapePtr),
        *static_cast<MatTransform*>(gtsXformPtr));
}

NIFLY_API void setTransform(void* theShape, void* buf) {
    NiShape* shape = static_cast<nifly::NiShape*>(theShape);
    MatTransform* xf = static_cast<MatTransform*>(buf);
    shape->SetTransformToParent(*xf);
}



/* ************************* BONES AND WEIGHTS ************************* */

NIFLY_API int getShapeBoneCount(void* theNif, void* theShape) {
    NifFile* nif = static_cast<NifFile*>(theNif);
    nifly::NiShape* shape = static_cast<nifly::NiShape*>(theShape);
    std::vector<int> bonelist;
    return nif->GetShapeBoneIDList(shape, bonelist);
}

NIFLY_API int getShapeBoneIDs(void* theNif, void* theShape, int* buf, int bufsize) {
    NifFile* nif = static_cast<NifFile*>(theNif);
    nifly::NiShape* shape = static_cast<nifly::NiShape*>(theShape);
    std::vector<int> bonelist;
    nif->GetShapeBoneIDList(shape, bonelist);
    for (int i = 0; i < bufsize && i < bonelist.size(); i++)
        buf[i] = bonelist[i];
    return int(bonelist.size());
}

NIFLY_API int getShapeBoneNames(void* theNif, void* theShape, char* buf, int buflen) 
// Returns a list of bone names the shape uses. List is separated by \n characters.
{
    NifFile* nif = static_cast<NifFile*>(theNif);
    nifly::NiShape* shape = static_cast<nifly::NiShape*>(theShape);
    std::vector<std::string> names;
    nif->GetShapeBoneList(shape, names);

    std::string s = "";
    for (auto& sn : names) {
        if (s.length() > 0) s += "\n";
        s += sn;
    }
    if (buf) {
        int copylen = std::min((int)buflen - 1, (int)s.length());
        s.copy(buf, copylen, 0);
        buf[copylen] = '\0';
    };

    return(int(s.length()));
}

NIFLY_API int getShapeBoneWeightsCount(void* theNif, void* theShape, int boneIndex) {
    /* Get the count of bone weights associated with the given bone. */
    NifFile* nif = static_cast<NifFile*>(theNif);
    nifly::NiShape* shape = static_cast<nifly::NiShape*>(theShape);

    std::unordered_map<uint16_t, float> boneWeights;
    return nif->GetShapeBoneWeights(shape, boneIndex, boneWeights);
}

NIFLY_API int getShapeBoneWeights(void* theNif, void* theShape, int boneIndex,
                                  struct VertexWeightPair* buf, int buflen) {
    /* Get the bone weights associated with the given bone for the given shape.
        boneIndex = index of bone in the list of bones associated with this shape 
        buf = Buffer to hold <vertex index, weight> for every vertex weighted to this bone.
    */
    NifFile* nif = static_cast<NifFile*>(theNif);
    nifly::NiShape* shape = static_cast<nifly::NiShape*>(theShape);

    std::vector<std::string> names;
    nif->GetShapeBoneList(shape, names);
    
    std::vector<int> bonelist;
    nif->GetShapeBoneIDList(shape, bonelist);

    std::unordered_map<uint16_t, float> boneWeights;
    int numWeights = nif->GetShapeBoneWeights(shape, boneIndex, boneWeights);

    int j = 0;
    for (const auto& [key, value] : boneWeights) {
        buf[j].vertex = key;
        buf[j++].weight = value;
        if (j >= buflen) break;
    }

    return numWeights;
}

NIFLY_API void addBoneToSkin(void* anim, const char* boneName,
    void* xformPtr, const char* parentName)
    /* Add the given bone to the skin for export. Note it is *not* added to the nif--use
    *  writeSkinToNif to update the nif.
    *  xformToParent may be omitted, in which case the bone transform comes from the
    *  reference skeleton (but then you don't need to make this call).
    *  parentName may be omitted if the bone has no parent.
    */
{
    std::string parent = std::string(parentName);
    AddCustomBoneRef(
        static_cast<AnimInfo*>(anim), 
        std::string(boneName), 
        &parent, 
        static_cast<MatTransform*>(xformPtr));
}


NIFLY_API void addBoneToShape(void* anim, void* theShape, const char* boneName, 
        void* xformPtr, const char* parentName)
/* Add the given bone to the shape for export. Note it is *not* added to the nif--use
*  writeSkinToNif to update the nif. 
*  TODO: Look at creating the node here directly.
*  xformToParent may be omitted, in which case the bone transform comes from the 
*  reference skeleton.
*  parentName may be omitted if the bone has no parent.
*/
{
    AddBoneToShape(static_cast<AnimInfo*>(anim), static_cast<NiShape*>(theShape),
        boneName, static_cast<MatTransform*>(xformPtr), parentName);
}

NIFLY_API void setShapeWeights(void* anim, void* theShape, const char* boneName,
    VertexWeightPair* vertWeights, int vertWeightLen, MatTransform* skinToBoneXform) {
    AnimWeight aw;
    for (int i = 0; i < vertWeightLen; i++) {
        aw.weights[vertWeights[i].vertex] = vertWeights[i].weight;
    };
    SetShapeWeights(static_cast<AnimInfo*>(anim), static_cast<NiShape*>(theShape), boneName, aw);
}

NIFLY_API void setShapeVertWeights(void* theFile, void* theShape,
    int vertIdx, const uint8_t* vertex_bones, const float* vertex_weights) {
    NifFile* nif = static_cast<NifFile*>(theFile);
    NiShape* shape = static_cast<nifly::NiShape*>(theShape);

    std::vector<uint8_t> boneids;
    std::vector<float> weights;
    for (int i = 0; i < 4; i++) {
        if (vertex_weights[i] > 0) {
            boneids.push_back(vertex_bones[i]);
            weights.push_back(vertex_weights[i]);
        };
    };
    nif->SetShapeVertWeights(shape->name.get(), vertIdx, boneids, weights);
}

NIFLY_API void setShapeBoneWeights(void* theFile, void* theShape,
    int boneIdx, VertexWeightPair* weights, int weightsLen)
{
    NifFile* nif = static_cast<NifFile*>(theFile);
    NiShape* shape = static_cast<nifly::NiShape*>(theShape);
    std::unordered_map<uint16_t, float> weight_map;
    for (int i = 0; i < weightsLen; i++) {
        weight_map[weights[i].vertex] = weights[i].weight;
    }
    nif->SetShapeBoneWeights(shape->name.get(), boneIdx, weight_map);
}

NIFLY_API void setShapeBoneIDList(void* theFile, void* shapeRef, int* boneIDList, int listLen)
{
    NifFile* nif = static_cast<NifFile*>(theFile);
    NiShape* shape = static_cast<nifly::NiShape*>(shapeRef);
    std::vector<int> bids;
    for (int i = 0; i < listLen; i++) {
        bids.push_back(boneIDList[i]);
    }
    nif->SetShapeBoneIDList(shape, bids);
}


/* ************************** SHADERS ************************** */

NIFLY_API int getShaderName(void* nifref, void* shaperef, char* buf, int buflen) {
/*
    Returns length of name string, -1 if there is no shader
*/
    NifFile* nif = static_cast<NifFile*>(nifref);
    NiShape* shape = static_cast<NiShape*>(shaperef);

    NiShader* shader = nif->GetShader(shape);

    if (!shader)
        return -1;
    else {
        strncpy_s(buf, buflen, shader->name.get().c_str(), buflen);
        buf[buflen - 1] = '\0';
    };

    return int(shader->name.get().length());
};

NIFLY_API uint32_t getShaderFlags1(void* nifref, void* shaperef) {
    NifFile* nif = static_cast<NifFile*>(nifref);
    NiShape* shape = static_cast<NiShape*>(shaperef);

    NiShader* shader = nif->GetShader(shape);
    if (!shader)
        return 0;
    else {
        BSLightingShaderProperty* bssh = dynamic_cast<BSLightingShaderProperty*>(shader);
        if (bssh) return bssh->shaderFlags1;
        BSEffectShaderProperty* bses = dynamic_cast<BSEffectShaderProperty*>(shader);
        if (bses) return bses->shaderFlags1;
        return 0;
    }
}

NIFLY_API uint32_t getShaderFlags2(void* nifref, void* shaperef) {
    NifFile* nif = static_cast<NifFile*>(nifref);
    NiShape* shape = static_cast<NiShape*>(shaperef);

    NiShader* shader = nif->GetShader(shape);
    
    if (!shader)
        return 0;
    else {
        BSShaderProperty* bssh = dynamic_cast<BSShaderProperty*>(shader);
        return (bssh ? bssh->shaderFlags2 : 0);
    };
}

NIFLY_API int getShaderTextureSlot(void* nifref, void* shaperef, int slotIndex, char* buf, int buflen) 
/*
* Return the filepath associated with the requested texture slot. 
* For BSEffectShaderProperty the slots are: 
*   0 = source texture
*   1 = normal map
*   2 = <not used>
*   3 = greyscale
*   4 = environment map
*   5 = environment mask
*/
{
    NifFile* nif = static_cast<NifFile*>(nifref);
    NiShape* shape = static_cast<NiShape*>(shaperef);
    NiShader* shader = nif->GetShader(shape);
    NiHeader hdr = nif->GetHeader();

    std::string texture;

    uint32_t val = nif->GetTextureSlot(shape, texture, slotIndex);

    if (buflen > 0) buf[0] = '\0';
    if (val == 0) return 0;

    if (buflen > 1) {
        memcpy(buf, texture.data(), std::min(texture.size(), static_cast<size_t>(buflen - 1)));
        buf[texture.size()] = '\0';
    }

    return static_cast<int>(texture.length());

    /* HOW OS RETURNS A PARTICULAR TEXTURE SLOT:
    * int NifFile::GetTextureSlot(NiShader* shader, std::string& outTexFile, int texIndex) const 
    */

    //auto textureSet = hdr.GetBlock(shader->TextureSetRef());
    //if (textureSet && slotIndex + 1 <= textureSet->textures.vec.size()) {
    //    outTexFile = textureSet->textures.vec[texIndex].get();
    //    return 1;
    //}

    //if (!textureSet) {
    //    auto effectShader = dynamic_cast<BSEffectShaderProperty*>(shader);
    //    if (effectShader) {
    //        switch (slotIndex) {
    //            case 0: outTexFile = effectShader->sourceTexture.get(); break;
    //            case 1: outTexFile = effectShader->normalTexture.get(); break;
    //            case 3: outTexFile = effectShader->greyscaleTexture.get(); break;
    //            case 4: outTexFile = effectShader->envMapTexture.get(); break;
    //            case 5: outTexFile = effectShader->envMaskTexture.get(); break;
    //        }

    //        return 2;
    //    }
    //}
    
};

NIFLY_API const char* getShaderBlockName(void* nifref, void* shaperef) {
    /* Returns name of the shader block property, e.g. "BSLightingShaderProperty"
    * Return value is null if shader is not BSLightingShader or BSEffectShader.
    */
    NifFile* nif = static_cast<NifFile*>(nifref);
    NiShape* shape = static_cast<NiShape*>(shaperef);

    NiShader* shader = nif->GetShader(shape);
    const char* blockName = nullptr;
 
    if (shader) {
        BSLightingShaderProperty* sp = dynamic_cast<BSLightingShaderProperty*>(shader);
        if (sp)
            blockName = sp->BlockName;
        else {
            BSEffectShaderProperty* ep = dynamic_cast<BSEffectShaderProperty*>(shader);
            if (ep)
                blockName = ep->BlockName;
        }
    };
    
    return blockName;
};

NIFLY_API uint32_t getShaderType(void* nifref, void* shaperef) {
/*
    Return value: 0 = no shader or not a LSLightingShader; anything else is the shader type
*/
    NifFile* nif = static_cast<NifFile*>(nifref);
    NiShape* shape = static_cast<NiShape*>(shaperef);

    NiShader* shader = nif->GetShader(shape);

    if (!shader)
        return 0;
    else
        return shader->GetShaderType();
};

NIFLY_API int getShaderAttrs(void* nifref, void* shaperef, struct BSLSPAttrs* buf)
/*
    Get attributes for a BSLightingShaderProperty
    Return value: 0 = success, 1 = no shader, or not a BSLightingShaderProperty
*/
{
    NifFile* nif = static_cast<NifFile*>(nifref);
    NiShape* shape = static_cast<NiShape*>(shaperef);

    NiShader* shader = nif->GetShader(shape);

    if (!shader) return 1;

    BSShaderProperty* bssh = dynamic_cast<BSShaderProperty*>(shader);
    BSLightingShaderProperty* bslsp = dynamic_cast<BSLightingShaderProperty*>(shader);

    if (!bslsp) return 1;

    NiTexturingProperty* txtProp = nif->GetTexturingProperty(shape);

    FillMemory(buf, sizeof(BSLSPAttrs), 0);

    buf->Shader_Type = shader->GetShaderType();
    if (bssh) buf->Shader_Flags_1 = bssh->shaderFlags1;
    if (bssh) buf->Shader_Flags_2 = bssh->shaderFlags2;
    buf->UV_Offset_U = shader->GetUVOffset().u;
    buf->UV_Offset_V = shader->GetUVOffset().v;
    buf->UV_Scale_U = shader->GetUVScale().u;
    buf->UV_Scale_V = shader->GetUVScale().v;
    buf->Emissive_Color_R = shader->GetEmissiveColor().r;
    buf->Emissive_Color_G = shader->GetEmissiveColor().g;
    buf->Emissive_Color_B = shader->GetEmissiveColor().b;
    buf->Emissive_Color_A = shader->GetEmissiveColor().a;
    buf->Emissmive_Mult = shader->GetEmissiveMultiple();
    buf->Environment_Map_Scale = shader->GetEnvironmentMapScale();
    if (txtProp) {
        NiSyncVector<ShaderTexDesc>* txtdesc = &txtProp->shaderTex;
        //buf->Tex_Clamp_Mode = txtdesc->data.clampMode;
    };
    buf->Alpha = shader->GetAlpha();
    buf->Glossiness = shader->GetGlossiness();
    buf->Spec_Color_R = shader->GetSpecularColor().x;
    buf->Spec_Color_G = shader->GetSpecularColor().y;
    buf->Spec_Color_B = shader->GetSpecularColor().z;
    buf->Spec_Str = shader->GetSpecularStrength();
    if (bslsp) {
        buf->Refraction_Str = bslsp->refractionStrength;
        buf->Soft_Lighting = bslsp->softlighting;
        buf->Rim_Light_Power = bslsp->rimlightPower;
        buf->Skin_Tint_Alpha = bslsp->skinTintAlpha;
        buf->Skin_Tint_Color_R = bslsp->skinTintColor[0];
        buf->Skin_Tint_Color_G = bslsp->skinTintColor[1];
        buf->Skin_Tint_Color_B = bslsp->skinTintColor[2];
    };

    return 0;
};

NIFLY_API int getEffectShaderAttrs(void* nifref, void* shaperef, struct BSESPAttrs* buf)
/*
    Get attributes for a BSEffectShaderProperty
    Return value: 0 = success, 1 = no shader, or not a BSEffectShaderProperty
*/
{
    NifFile* nif = static_cast<NifFile*>(nifref);
    NiShape* shape = static_cast<NiShape*>(shaperef);

    NiShader* shader = nif->GetShader(shape);

    if (!shader) return 1;

    BSShaderProperty* bssh = dynamic_cast<BSShaderProperty*>(shader);
    BSEffectShaderProperty* bsesp = dynamic_cast<BSEffectShaderProperty*>(shader);

    if (!bsesp) return 1;

    FillMemory(buf, sizeof(BSESPAttrs), 0);

    if (bssh) buf->Shader_Flags_1 = bssh->shaderFlags1;
    if (bssh) buf->Shader_Flags_2 = bssh->shaderFlags2;
    buf->UV_Offset_U = shader->GetUVOffset().u;
    buf->UV_Offset_V = shader->GetUVOffset().v;
    buf->UV_Scale_U = shader->GetUVScale().u;
    buf->UV_Scale_V = shader->GetUVScale().v;
    buf->Tex_Clamp_Mode = bsesp->textureClampMode;
    //buf->Lighting_Influence = bsesp->light;
    //buf->Env_Map_Min_LOD = bsesp->getEnvmapMinLOD();
    buf->Falloff_Start_Angle = bsesp->falloffStartAngle;
    buf->Falloff_Stop_Angle = bsesp->falloffStopAngle;
    buf->Falloff_Start_Opacity = bsesp->falloffStartOpacity;
    buf->Falloff_Stop_Opacity = bsesp->falloffStopOpacity;
    buf->Emissive_Color_R = shader->GetEmissiveColor().r;
    buf->Emissive_Color_G = shader->GetEmissiveColor().g;
    buf->Emissive_Color_B = shader->GetEmissiveColor().b;
    buf->Emissive_Color_A = shader->GetEmissiveColor().a;
    buf->Emissmive_Mult = shader->GetEmissiveMultiple();
    buf->Soft_Falloff_Depth = bsesp->softFalloffDepth;
    buf->Env_Map_Scale = shader->GetEnvironmentMapScale();

    return 0;
};

NIFLY_API int getAlphaProperty(void* nifref, void* shaperef, AlphaPropertyBuf* bufptr) {
    NifFile* nif = static_cast<NifFile*>(nifref);
    NiShape* shape = static_cast<NiShape*>(shaperef);
    if (shape->HasAlphaProperty()) {
        NiAlphaProperty* alph = nif->GetAlphaProperty(shape);
        bufptr->flags = alph->flags;
        bufptr->threshold = alph->threshold;
        return 1;
    }
    else
        return 0;
}

NIFLY_API void setAlphaProperty(void* nifref, void* shaperef, AlphaPropertyBuf* bufptr) {
    NifFile* nif = static_cast<NifFile*>(nifref);
    NiShape* shape = static_cast<NiShape*>(shaperef);

    auto alphaProp = std::make_unique<NiAlphaProperty>();
    alphaProp->flags = bufptr->flags;
    alphaProp->threshold = bufptr->threshold;
    nif->AssignAlphaProperty(shape, std::move(alphaProp));
}

NIFLY_API void setShaderName(void* nifref, void* shaperef, char* name) {
    NifFile* nif = static_cast<NifFile*>(nifref);
    NiShape* shape = static_cast<NiShape*>(shaperef);

    NiShader* shader = nif->GetShader(shape);
    shader->name.get() = name;
};

NIFLY_API void setShaderType(void* nifref, void* shaperef, uint32_t shaderType) {
    NifFile* nif = static_cast<NifFile*>(nifref);
    NiShape* shape = static_cast<NiShape*>(shaperef);

    NiShader* shader = nif->GetShader(shape);
    return shader->SetShaderType(shaderType);
};

NIFLY_API void setShaderFlags1(void* nifref, void* shaperef, uint32_t flags) {
    NifFile* nif = static_cast<NifFile*>(nifref);
    NiShape* shape = static_cast<NiShape*>(shaperef);

    NiShader* shader = nif->GetShader(shape);
    BSShaderProperty* bssh = dynamic_cast<BSShaderProperty*>(shader);

    if (bssh) bssh->shaderFlags1 = flags;
}

NIFLY_API void setShaderFlags2(void* nifref, void* shaperef, uint32_t flags) {
    NifFile* nif = static_cast<NifFile*>(nifref);
    NiShape* shape = static_cast<NiShape*>(shaperef);

    NiShader* shader = nif->GetShader(shape);
    BSShaderProperty* bssh = dynamic_cast<BSShaderProperty*>(shader);

    if (bssh) bssh->shaderFlags2 = flags;
}

NIFLY_API void setShaderTextureSlot(void* nifref, void* shaperef, int slotIndex, const char* buf) {
    NifFile* nif = static_cast<NifFile*>(nifref);
    NiShape* shape = static_cast<NiShape*>(shaperef);

    std::string texture = buf;

    nif->SetTextureSlot(shape, texture, slotIndex);

/* HOW OS SETS TEXTURE SLOT
void NifFile::SetTextureSlot(NiShader* shader, std::string& inTexFile, int texIndex) {
    auto textureSet = hdr.GetBlock(shader->TextureSetRef());
    if (textureSet && texIndex + 1 <= textureSet->textures.vec.size()) {
        textureSet->textures.vec[texIndex].get() = inTexFile;
        return;
    }

    if (!textureSet) {
        auto effectShader = dynamic_cast<BSEffectShaderProperty*>(shader);
        if (effectShader) {
            switch (texIndex) {
                case 0: effectShader->sourceTexture.get() = inTexFile; break;
                case 1: effectShader->normalTexture.get() = inTexFile; break;
                case 3: effectShader->greyscaleTexture.get() = inTexFile; break;
                case 4: effectShader->envMapTexture.get() = inTexFile; break;
                case 5: effectShader->envMaskTexture.get() = inTexFile; break;
            }
        }
    }
}
*/
}

NIFLY_API void setShaderAttrs(void* nifref, void* shaperef, struct BSLSPAttrs* buf) {
    NifFile* nif = static_cast<NifFile*>(nifref);
    NiShape* shape = static_cast<NiShape*>(shaperef);

    NiShader* shader = nif->GetShader(shape);;
    BSShaderProperty* bssh = dynamic_cast<BSShaderProperty*>(shader);
    BSLightingShaderProperty* bslsp = dynamic_cast<BSLightingShaderProperty*>(shader);
    NiTexturingProperty* txtProp = nif->GetTexturingProperty(shape);

    shader->SetShaderType(buf->Shader_Type);
    if (bssh) {
        bssh->shaderFlags1 = buf->Shader_Flags_1;
        bssh->shaderFlags2 = buf->Shader_Flags_2;
    };
    //shader->SetUVOffset( = buf->UV_Offset_U = ;
    //buf->UV_Offset_V = shader->GetUVOffset().v;
    //buf->UV_Scale_U = shader->GetUVScale().u;
    //buf->UV_Scale_V = shader->GetUVScale().v;
    Color4 col = Color4(buf->Emissive_Color_R,
        buf->Emissive_Color_G,
        buf->Emissive_Color_B,
        buf->Emissive_Color_A);
    shader->SetEmissiveColor(col);
    shader->SetEmissiveMultiple(buf->Emissmive_Mult);
    if (txtProp) {
        NiSyncVector<ShaderTexDesc>* txtdesc = &txtProp->shaderTex;
        //txtdesc->data.clampMode = buf->Tex_Clamp_Mode;
    };
    //shader->SetAlpha(buf->Alpha);
    shader->SetGlossiness(buf->Glossiness);
    Vector3 specCol = Vector3(buf->Spec_Color_R, buf->Spec_Color_G, buf->Spec_Color_B);
    shader->SetSpecularColor(specCol);
    shader->SetSpecularStrength(buf->Spec_Str);
    if (bslsp) {
        bslsp->refractionStrength = buf->Refraction_Str;
        bslsp->softlighting = buf->Soft_Lighting;
        bslsp->rimlightPower = buf->Rim_Light_Power;
        bslsp->skinTintAlpha = buf->Skin_Tint_Alpha;
        bslsp->skinTintColor[0] = buf->Skin_Tint_Color_R;
        bslsp->skinTintColor[1] = buf->Skin_Tint_Color_G;
        bslsp->skinTintColor[2] = buf->Skin_Tint_Color_B;
    };
};

NIFLY_API void setEffectShaderAttrs(void* nifref, void* shaperef, struct BSESPAttrs* buf) {
    NifFile* nif = static_cast<NifFile*>(nifref);
    NiShape* shape = static_cast<NiShape*>(shaperef);

    NiShader* shader = nif->GetShader(shape);;
    BSShaderProperty* bssh = dynamic_cast<BSShaderProperty*>(shader);
    BSEffectShaderProperty* bsesp= dynamic_cast<BSEffectShaderProperty*>(shader);
    NiTexturingProperty* txtProp = nif->GetTexturingProperty(shape);

    if (bssh) {
        bssh->shaderFlags1 = buf->Shader_Flags_1;
        bssh->shaderFlags2 = buf->Shader_Flags_2;
    };
    Color4 col = Color4(buf->Emissive_Color_R,
        buf->Emissive_Color_G,
        buf->Emissive_Color_B,
        buf->Emissive_Color_A);
    shader->SetEmissiveColor(col);
    shader->SetEmissiveMultiple(buf->Emissmive_Mult);
    if (txtProp) {
        NiSyncVector<ShaderTexDesc>* txtdesc = &txtProp->shaderTex;
        //txtdesc->data.clampMode = buf->Tex_Clamp_Mode;
    };
    //shader->SetAlpha(buf->Alpha);
    if (bsesp) {
        bsesp->textureClampMode = buf->Tex_Clamp_Mode;
        bsesp->falloffStartAngle = buf->Falloff_Start_Angle;
        bsesp->falloffStopAngle = buf->Falloff_Stop_Angle;
        bsesp->falloffStartOpacity = buf->Falloff_Start_Opacity;
        bsesp->falloffStopOpacity = buf->Falloff_Stop_Opacity;
        bsesp->softFalloffDepth = buf->Soft_Falloff_Depth;
        bsesp->envMapScale = buf->Env_Map_Scale;
    };
};


/* ******************** SEGMENTS AND PARTITIONS ****************************** */

NIFLY_API int segmentCount(void* nifref, void* shaperef) {
    /*
        Return count of segments associated with the shape.
        If not FO4 nif or no segments returns 0
    */
    NifFile* nif = static_cast<NifFile*>(nifref);
    NiShape* shape = static_cast<NiShape*>(shaperef);
    NifSegmentationInfo segInfo;
    std::vector<int> triParts;
    if (nif->GetShapeSegments(shape, segInfo, triParts))
        return int(segInfo.segs.size());
    else
        return 0;
}

NIFLY_API int getSegmentFile(void* nifref, void* shaperef, char* buf, int buflen) {
    /*
        Return segment file associated with the shape
        If not FO4 nif or no segments returns ''
    */
    NifFile* nif = static_cast<NifFile*>(nifref);
    NiShape* shape = static_cast<NiShape*>(shaperef);
    NifSegmentationInfo segInfo;
    std::vector<int> triParts;
    if (nif->GetShapeSegments(shape, segInfo, triParts)) {
        if (buflen > 0) {
            int copylen = std::min((int)buflen - 1, (int)segInfo.ssfFile.size());
            segInfo.ssfFile.copy(buf, copylen, 0);
            buf[copylen] = '\0';
        }
        return int(segInfo.ssfFile.size());
    }
    else {
        if (buflen > 0) buf[0] = '\0';
        return 0;
    }
}

NIFLY_API int getSegments(void* nifref, void* shaperef, int* segments, int segLen) {
    /*
        Return segments associated with a shape. Only for FO4-style nifs.
        segments -> (int ID, int count_of_subsegments)...
    */
    NifFile* nif = static_cast<NifFile*>(nifref);
    NiShape* shape = static_cast<NiShape*>(shaperef);
    NifSegmentationInfo segInfo;
    std::vector<int> indices;

    if (nif->GetShapeSegments(shape, segInfo, indices)) {
        for (int i = 0, j = 0; i < segLen * 2 && j < int(segInfo.segs.size()); j++) {
            segments[i++] = segInfo.segs[j].partID;
            segments[i++] = int(segInfo.segs[j].subs.size());
        }
        return int(segInfo.segs.size());
    }
    return 0;
}

NIFLY_API int getSubsegments(void* nifref, void* shaperef, int segID, uint32_t* segments, int segLen) {
    /*
        Return subsegments associated with a shape. Only for FO4-style nifs.
        segments -> (int ID, userSlotID, material)...
    */
    NifFile* nif = static_cast<NifFile*>(nifref);
    NiShape* shape = static_cast<NiShape*>(shaperef);
    NifSegmentationInfo segInfo;
    std::vector<int> indices;

    if (nif->GetShapeSegments(shape, segInfo, indices)) {
        for (auto& s: segInfo.segs) {
            if (s.partID == segID) {
                for (int i = 0, j = 0; i < segLen * 3 && j < int(s.subs.size()); j++) {
                    segments[i++] = s.subs[j].partID;
                    segments[i++] = s.subs[j].userSlotID;
                    segments[i++] = s.subs[j].material;
                }
                return int(s.subs.size());
            }
        }
        return 0;
    }
return 0;
}

NIFLY_API int getPartitions(void* nifref, void* shaperef, uint16_t* partitions, int partLen) {
    /*
        Return a list of partitions associated with the shape. Only for skyrim-style nifs.
        partitions = (uint16 flags, uint16 partID)... where partID is the body part ID
    */
    NifFile* nif = static_cast<NifFile*>(nifref);
    NiShape* shape = static_cast<NiShape*>(shaperef);
    NiVector<BSDismemberSkinInstance::PartitionInfo> partInfos;
    NifSegmentationInfo segInfo;
    std::vector<int> indices;

    GetPartitions(nif, shape, partInfos, indices);

    for (int i = 0, j = 0; i < partLen * 2 && j < int(partInfos.size()); j++) {
        partitions[i++] = partInfos[j].flags;
        partitions[i++] = partInfos[j].partID;
    }
    return int(partInfos.size());
}

NIFLY_API int getPartitionTris(void* nifref, void* shaperef, uint16_t* tris, int triLen) {
    /*
        Return a list of segment indices matching 1-1 with the shape's triangles.
        Used for both skyrim and fo4-style nifs
    */
    NifFile* nif = static_cast<NifFile*>(nifref);
    NiShape* shape = static_cast<NiShape*>(shaperef);
    NiVector<BSDismemberSkinInstance::PartitionInfo> partInfos;
    std::vector<int> indices;

    GetPartitions(nif, shape, partInfos, indices);

    for (int i = 0; i < triLen && i < int(indices.size()); i++) {
        tris[i] = indices[i];
    }
    return int(indices.size());
}

NIFLY_API void setPartitions(void* nifref, void* shaperef,
    uint16_t* partData, int partDataLen,
    uint16_t* tris, int triLen)
    /* partData = (uint16 flags, uint16 partID)... where partID is the body part ID
    * partDataLen = length of the buffer in uint16s
    * tris = list of segment indices matching 1-1 with shape triangles
    * 
        >>Needs to be called AFTER bone weights are set
    */
{
    NifFile* nif = static_cast<NifFile*>(nifref);
    NiShape* shape = static_cast<NiShape*>(shaperef);
    NiVector<BSDismemberSkinInstance::PartitionInfo> partInfos;
    std::vector<int> triParts;

    for (int i = 0; i < partDataLen*2; i += 2) {
        BSDismemberSkinInstance::PartitionInfo p;
        p.flags = static_cast<PartitionFlags>(partData[i]);
        p.partID = partData[i+1];
        partInfos.push_back(p);
    }

    for (int i = 0; i < triLen; i++) {
        triParts.push_back(tris[i]);
    }

    nif->SetShapePartitions(shape, partInfos, triParts, true);
    nif->UpdateSkinPartitions(shape);
}

NIFLY_API void setSegments(void* nifref, void* shaperef,
    uint16_t* segData, int segDataLen,
    uint32_t* subsegData, int subsegDataLen,
    uint16_t* tris, int triLen,
    const char* filename)
    /*
    * Create segments and subsegments in the nif
    * segData = [part_id, ...] list of internal IDs for each segment
    * subsegData = [[part_id, parent_id, user_slot, material], ...]
    * tris = [part_id, ...] matches 1:1 with the shape's tris, indicates which subsegment
    *   it's a part of
    * filename = null-terminated filename
    */
{
    NifFile* nif = static_cast<NifFile*>(nifref);
    NiShape* shape = static_cast<NiShape*>(shaperef);

    try {
        NifSegmentationInfo inf;
        inf.ssfFile = filename;
        std::unordered_set<uint32_t> allParts;

        for (int i = 0; i < segDataLen; i++) {
            NifSegmentInfo* seg = new NifSegmentInfo();
            seg->partID = segData[i];
            inf.segs.push_back(*seg);
            allParts.insert(seg->partID);
        }

        for (int i = 0, j = 0; i < subsegDataLen; i++) {
            NifSubSegmentInfo sseg;
            sseg.partID = subsegData[j++];
            uint32_t parentID = subsegData[j++];
            sseg.userSlotID = subsegData[j++];
            sseg.material = subsegData[j++];

            for (auto& seg : inf.segs) {
                if (seg.partID == parentID) {
                    seg.subs.push_back(sseg);
                    allParts.insert(sseg.partID);
                    break;
                }
            }
        }

        std::vector<int> triParts;
        for (int i = 0; i < triLen; i++) {
            // Checking for invalid segment references explicitly because the try/catch isn't working
            if (allParts.find(tris[i]) == allParts.end()) {
                niflydll::LogWrite("ERROR: Tri list references invalid segment, segments are not correct");
                return;
            }
            else
                triParts.push_back(tris[i]);
        }
        nif->SetShapeSegments(shape, inf, triParts);
        nif->UpdateSkinPartitions(shape);
    }
    catch (std::exception e) {
        niflydll::LogWrite("Error in setSegments, segments may not be correct");
    }
}

/* ************************ VERTEX COLORS AND ALPHA ********************* */

NIFLY_API int getColorsForShape(void* nifref, void* shaperef, float* colors, int colorLen) {
    /*
        Return vertex colors.
        colorLen = # of floats buffer can hold, has to be 4x number of colors
        Return value is # of colors, which is # of vertices.
    */
    NifFile* nif = static_cast<NifFile*>(nifref);
    NiShape* shape = static_cast<NiShape*>(shaperef);
    const std::vector<Color4>* theColors = nif->GetColorsForShape(shape->name.get());
    for (int i = 0, j = 0; j < colorLen && i < theColors->size(); i++) {
        colors[j++] = theColors->at(i).r;
        colors[j++] = theColors->at(i).g;
        colors[j++] = theColors->at(i).b;
        colors[j++] = theColors->at(i).a;
    }
    return int(theColors->size());
}

NIFLY_API void setColorsForShape(void* nifref, void* shaperef, float* colors, int colorLen) {
    /*
        Set vertex colors.
        colorLen = # of color values in the buf, must be same as # of vertices
    */
    NifFile* nif = static_cast<NifFile*>(nifref);
    NiShape* shape = static_cast<NiShape*>(shaperef);
    std::vector<Color4> theColors;
    for (int i = 0, j = 0; i < colorLen; i++) {
        Color4 c;
        c.r = colors[j++];
        c.g = colors[j++];
        c.b = colors[j++];
        c.a = colors[j++];
        theColors.push_back(c);
    }
    nif->SetColorsForShape(shape->name.get(), theColors);
}

/* ***************************** EXTRA DATA ***************************** */

const char* ClothExtraDataName = "Binary Data";
const int ClothExtraDataNameLen = 11;

int getClothExtraDataLen(void* nifref, void* shaperef, int idx, int* namelen, int* valuelen)
/* Treats the BSClothExtraData nodes in the nif like an array--idx indicates
    which to return (0-based).
    (Probably there can be only one per file but code allows for more)
    */
{
    NifFile* nif = static_cast<NifFile*>(nifref);
    NiHeader hdr = nif->GetHeader();

    //NiShape* shape = static_cast<NiShape*>(shaperef);
    NiAVObject* source = nullptr;
    if (shaperef)
        source = static_cast<NiAVObject*>(shaperef);
    else
        source = nif->GetRootNode();

    int i = idx;
    for (auto& extraData : source->extraDataRefs) {
        BSClothExtraData* clothData = hdr.GetBlock<BSClothExtraData>(extraData);
        if (clothData) {
            if (i == 0) {
                *valuelen = int(clothData->data.size());
                *namelen = ClothExtraDataNameLen;
                return 1;
            }
            else
                i--;
        }
    }
    return 0;
};

int getClothExtraData(void* nifref, void* shaperef, int idx, char* name, int namelen, char* buf, int buflen)
/* Treats the BSClothExtraData nodes in the nif like an array--idx indicates
    which to return (0-based).
    (Probably there can be only one per file but code allows for more)
    Returns 1 if the extra data was found at requested index
    */
{
    NifFile* nif = static_cast<NifFile*>(nifref);
    NiHeader hdr = nif->GetHeader();

    //NiShape* shape = static_cast<NiShape*>(shaperef);
    NiAVObject* source = nullptr;
    if (shaperef)
        source = static_cast<NiAVObject*>(shaperef);
    else
        source = nif->GetRootNode();

    int i = idx;
    for (auto& extraData : source->extraDataRefs) {
        BSClothExtraData* clothData = hdr.GetBlock<BSClothExtraData>(extraData);
        if (clothData) {
            if (i == 0) {
                for (int j = 0; j < buflen && j < clothData->data.size(); j++) {
                    buf[j] = clothData->data[j];
                }
                strncpy_s(name, namelen, ClothExtraDataName, ClothExtraDataNameLen);
                return 1;
            }
            else
                i--;
        }
    }
    return 0;
};

void setClothExtraData(void* nifref, void* shaperef, char* name, char* buf, int buflen) {
    NifFile* nif = static_cast<NifFile*>(nifref);
    NiAVObject* target = nullptr;
    target = nif->GetRootNode();

    if (target) {
        auto clothData = std::make_unique<BSClothExtraData>();
        for (int i = 0; i < buflen; i++) {
            clothData->data.push_back(buf[i]);
        }
        int id = nif->GetHeader().AddBlock(std::move(clothData));
        if (id != 0xFFFFFFFF) {
            target->extraDataRefs.AddBlockRef(id);
        }
    }
};

int getStringExtraDataLen(void* nifref, void* shaperef, int idx, int* namelen, int* valuelen)
/* Treats the NiStringExtraData nodes in the nif like an array--idx indicates
    which to return (0-based).
    */
{
    NifFile* nif = static_cast<NifFile*>(nifref);
    NiHeader hdr = nif->GetHeader();

    //NiShape* shape = static_cast<NiShape*>(shaperef);
    NiAVObject* source = nullptr;
    if (shaperef)
        source = static_cast<NiAVObject*>(shaperef);
    else
        source = nif->GetRootNode();

    int i = idx;
    for (auto& extraData : source->extraDataRefs) {
        NiStringExtraData* strData = hdr.GetBlock<NiStringExtraData>(extraData);
        if (strData) {
            if (i == 0) {
                *namelen = int(strData->name.get().size());
                *valuelen = int(strData->stringData.get().size());
                return 1;
            }
            else
                i--;
        }
    }
    return 0;
};

int getStringExtraData(void* nifref, void* shaperef, int idx, char* name, int namelen, char* buf, int buflen)
/* Treats the NiStringExtraData nodes in the nif like an array--idx indicates
    which to return (0-based).
    */
{
    NifFile* nif = static_cast<NifFile*>(nifref);
    NiHeader hdr = nif->GetHeader();

    //NiShape* shape = static_cast<NiShape*>(shaperef);
    NiAVObject* source = nullptr;
    if (shaperef)
        source = static_cast<NiAVObject*>(shaperef);
    else
        source = nif->GetRootNode();

    int i = idx;
    for (auto& extraData : source->extraDataRefs) {
        NiStringExtraData* strData = hdr.GetBlock<NiStringExtraData>(extraData);
        if (strData) {
            if (i == 0) {
                strncpy_s(name, namelen, strData->name.get().c_str(), namelen - 1);
                strncpy_s(buf, buflen, strData->stringData.get().c_str(), buflen - 1);
                return 1;
            }
            else
                i--;
        }
    }
    return 0;
};

void setStringExtraData(void* nifref, void* shaperef, char* name, char* buf) {
    NifFile* nif = static_cast<NifFile*>(nifref);
    NiAVObject* target = nullptr;
    if (shaperef)
        target = static_cast<NiAVObject*>(shaperef);
    else
        target = nif->GetRootNode();
    
    if (target) {
        auto strdata = std::make_unique<NiStringExtraData>();
        strdata->name.get() = name;
        strdata->stringData.get() = buf;
        nif->AssignExtraData(target, std::move(strdata));
    }
};

int getBGExtraDataLen(void* nifref, void* shaperef, int idx, int* namelen, int* datalen)
/* Treats the NiBehaviorGraphExtraData nodes in the nif like an array--idx indicates
    which to return (0-based).
    Returns T/F depending on whether extra data exists
    */
{
    NifFile* nif = static_cast<NifFile*>(nifref);
    NiHeader hdr = nif->GetHeader();

    //NiShape* shape = static_cast<NiShape*>(shaperef);
    NiAVObject* source = nullptr;
    if (shaperef)
        source = static_cast<NiAVObject*>(shaperef);
    else
        source = nif->GetRootNode();

    int i = idx;
    for (auto& extraData : source->extraDataRefs) {
        BSBehaviorGraphExtraData* bgData = hdr.GetBlock<BSBehaviorGraphExtraData>(extraData);
        if (bgData) {
            if (i == 0) {
                *namelen = int(bgData->name.get().size());
                *datalen = int(bgData->behaviorGraphFile.get().size());
                return 1;
            }
            else
                i--;
        }
    }
    return 0;
};
int getBGExtraData(void* nifref, void* shaperef, int idx, char* name, int namelen, 
        char* buf, int buflen, uint16_t* ctrlBaseSkelP)
/* Treats the NiBehaviorGraphExtraData nodes in the nif like an array--idx indicates
    which to return (0-based).
    */
{
    NifFile* nif = static_cast<NifFile*>(nifref);
    NiHeader hdr = nif->GetHeader();

    //NiShape* shape = static_cast<NiShape*>(shaperef);
    NiAVObject* source = nullptr;
    if (shaperef)
        source = static_cast<NiAVObject*>(shaperef);
    else
        source = nif->GetRootNode();

    int i = idx;
    for (auto& extraData : source->extraDataRefs) {
        BSBehaviorGraphExtraData* bgData = hdr.GetBlock<BSBehaviorGraphExtraData>(extraData);
        if (bgData) {
            if (i == 0) {
                strncpy_s(name, namelen, bgData->name.get().c_str(), namelen - 1);
                strncpy_s(buf, buflen, bgData->behaviorGraphFile.get().c_str(), buflen - 1);
                *ctrlBaseSkelP = bgData->controlsBaseSkel;
                return 1;
            }
            else
                i--;
        }
    }
    return 0;
};

int getInvMarker(void* nifref, char* name, int namelen, int* rot, float* zoom)
/* 
* Returns the InvMarker node data, if any. Assumes there is only one.
*   name = receives the name--will be null terminated
*   namelen = length of the name buffer
*   rot = int[3] for X, Y, Z
*   zoom = zoom value
* Return value = true/false whether a BSInvMarker exists
*/
{
    NifFile* nif = static_cast<NifFile*>(nifref);
    NiHeader hdr = nif->GetHeader();
    NiAVObject* source = nif->GetRootNode();

    for (auto& extraData : source->extraDataRefs) {
        BSInvMarker* invm = hdr.GetBlock<BSInvMarker>(extraData);
        if (invm) {
            strncpy_s(name, namelen, invm->name.get().c_str(), namelen - 1);
            rot[0] = invm->rotationX;
            rot[1] = invm->rotationY;
            rot[2] = invm->rotationZ;
            *zoom = invm->zoom;
            return 1;
        }
    }
    return 0;
};

int getFurnMarker(void* nifref, int index, FurnitureMarkerBuf* buf) {
    NifFile* nif = static_cast<NifFile*>(nifref);
    NiHeader hdr = nif->GetHeader();
    NiAVObject* source = nif->GetRootNode();

    int c = 0;

    for (auto& ed : source->extraDataRefs) {
        BSFurnitureMarker* fm = hdr.GetBlock<BSFurnitureMarker>(ed);
        if (fm) {
            for (auto pos : fm->positions) {
                if (c == index) {
                    for (int i = 0; i < 3; i++) buf->offset[i] = pos.offset[i];
                    buf->heading = pos.heading;
                    buf->animationType = pos.animationType;
                    buf->entryPoints = pos.entryPoints;

                    return 1;
                }
                c++;
            };
        };
    };
    return 0;
}

void setFurnMarkers(void* nifref, int buflen, FurnitureMarkerBuf* buf) {
    NifFile* nif = static_cast<NifFile*>(nifref);

    auto fm = std::make_unique<BSFurnitureMarkerNode>();
    
    for (int i=0; i < buflen; i++) {
        FurniturePosition pos;
        for (int j = 0; j < 3; j++) pos.offset[j] = buf[i].offset[j];
        pos.heading = buf[i].heading;
        pos.animationType = buf[i].animationType;
        pos.entryPoints = buf[i].entryPoints;
        fm->positions.push_back(pos);
    }
    nif->AssignExtraData(nif->GetRootNode(), std::move(fm));
}

void setInvMarker(void* nifref, const char* name, int* rot, float* zoom)
{
    NifFile* nif = static_cast<NifFile*>(nifref);
    auto inv = std::make_unique<BSInvMarker>();
    inv->name.get() = name;
    inv->rotationX = rot[0];
    inv->rotationY = rot[1];
    inv->rotationZ = rot[2];
    inv->zoom = *zoom;
    nif->AssignExtraData(nif->GetRootNode(), std::move(inv));
}

int getBSXFlags(void* nifref, int* buf)
{
    NifFile* nif = static_cast<NifFile*>(nifref);
    NiHeader hdr = nif->GetHeader();
    NiAVObject* source = nif->GetRootNode();

    for (auto& extraData : source->extraDataRefs) {
        BSXFlags* f = hdr.GetBlock<BSXFlags>(extraData);
        if (f) {
            *buf = f->integerData;
            return 1;
        }
    }
    return 0;
}

void setBSXFlags(void* nifref, const char* name, uint32_t flags)
{
    NifFile* nif = static_cast<NifFile*>(nifref);
    auto bsx = std::make_unique<BSXFlags>();
    bsx->name.get() = name;
    bsx->integerData = flags;
    nif->AssignExtraData(nif->GetRootNode(), std::move(bsx));
}

void setBGExtraData(void* nifref, void* shaperef, char* name, char* buf, int controlsBaseSkel) {
    NifFile* nif = static_cast<NifFile*>(nifref);
    NiAVObject* target = nullptr;
    if (shaperef)
        target = static_cast<NiAVObject*>(shaperef);
    else
        target = nif->GetRootNode();

    if (target) {
        auto strdata = std::make_unique<BSBehaviorGraphExtraData>();
        strdata->name.get() = name;
        strdata->behaviorGraphFile.get() = buf;
        strdata->controlsBaseSkel = controlsBaseSkel;
        nif->AssignExtraData(target, std::move(strdata));
    }
};

/* ********************* ERROR REPORTING ********************* */

void clearMessageLog() {
    niflydll::LogInit();
};

int getMessageLog(char* buf, int buflen) {
    if (buf)
        return niflydll::LogGet(buf, buflen);
    else
        return niflydll::LogGetLen();
    //return niflydll::LogGetLen();
}

/* ***************************** COLLISION OBJECTS ***************************** */

void* getCollision(void* nifref, void* noderef) {
    NifFile* nif = static_cast<NifFile*>(nifref);
    NiHeader hdr = nif->GetHeader();
    nifly::NiNode* node = static_cast<nifly::NiNode*>(noderef);

    return hdr.GetBlock(node->collisionRef);
};

NIFLY_API void* addCollision(void* nifref, void* targetref, int body_index, int flags) {
    NifFile* nif = static_cast<NifFile*>(nifref);
    NiHeader hdr = nif->GetHeader();
    nifly::bhkRigidBody* theBody = nif->GetHeader().GetBlock<bhkRigidBody>(body_index);
    nifly::NiNode* targ;
    if (targetref)
        targ = static_cast<nifly::NiNode*>(targetref);
    else
        targ = nif->GetRootNode();

    auto c = std::make_unique<bhkCollisionObject>();
    c->bodyRef.index = body_index;
    c->targetRef.index = nif->GetHeader().GetBlockID(targ);
    c->flags = flags;
    uint32_t newid = nif->GetHeader().AddBlock(std::move(c));
    targ->collisionRef.index = newid;
    
    return nif->GetHeader().GetBlock(targ->collisionRef);
};

NIFLY_API int getCollBlockname(void* node, char* buf, int buflen) {
    nifly::bhkCollisionObject* theNode = static_cast<nifly::bhkCollisionObject*>(node);
    if (theNode) {
        std::string name = theNode->GetBlockName();
        int copylen = std::min((int)buflen - 1, (int)name.length());
        name.copy(buf, copylen, 0);
        buf[copylen] = '\0';
        return int(name.length());
    }
    else
        return 0;
}

NIFLY_API int getCollBodyID(void* nifref, void* node) {
    NifFile* nif = static_cast<NifFile*>(nifref);
    NiHeader hdr = nif->GetHeader();
    nifly::bhkCollisionObject* theNode = static_cast<nifly::bhkCollisionObject*>(node);
    if (theNode)
        return theNode->bodyRef.index;
    else
        return 0;
}

NIFLY_API int addRigidBody(void* nifref, const char* type, uint32_t collShapeIndex, BHKRigidBodyBuf* buf) {
    NifFile* nif = static_cast<NifFile*>(nifref);
    NiHeader hdr = nif->GetHeader();

    std::unique_ptr<bhkRigidBody> theBody;
    if (strcmp(type, "bhkRigidBodyT") == 0)
        theBody = std::make_unique<bhkRigidBodyT>();
    else
        theBody = std::make_unique<bhkRigidBody>();

    theBody->collisionFilter.layer = buf->collisionFilter_layer;
    theBody->collisionFilter.flagsAndParts = buf->collisionFilter_flags;
    theBody->collisionFilter.group = buf->collisionFilter_group;
    theBody->broadPhaseType = buf->broadPhaseType;
    theBody->prop.data = buf->prop_data;
    theBody->prop.size = buf->prop_size;
    theBody->prop.capacityAndFlags = buf->prop_flags;
    theBody->collisionResponse = static_cast<hkResponseType>(buf->collisionResponse);
    theBody->processContactCallbackDelay = buf->processContactCallbackDelay;
    theBody->collisionFilterCopy.layer = buf->collisionFilterCopy_layer;
    theBody->collisionFilterCopy.flagsAndParts = buf->collisionFilterCopy_flags;
    theBody->collisionFilterCopy.group = buf->collisionFilterCopy_group;
    theBody->translation.x = buf->translation_x;
    theBody->translation.y = buf->translation_y;
    theBody->translation.z = buf->translation_z;
    theBody->translation.w = buf->translation_w;
    theBody->rotation.x = buf->rotation_x;
    theBody->rotation.y = buf->rotation_y;
    theBody->rotation.z = buf->rotation_z;
    theBody->rotation.w = buf->rotation_w;
    theBody->linearVelocity.x = buf->linearVelocity_x;
    theBody->linearVelocity.y = buf->linearVelocity_y;
    theBody->linearVelocity.z = buf->linearVelocity_z;
    theBody->linearVelocity.w = buf->linearVelocity_w;
    theBody->angularVelocity.x = buf->angularVelocity_x;
    theBody->angularVelocity.y = buf->angularVelocity_y;
    theBody->angularVelocity.z = buf->angularVelocity_z;
    theBody->angularVelocity.w = buf->angularVelocity_w;
    for (int i = 0; i < 12; i++) theBody->inertiaMatrix[i] = buf->inertiaMatrix[i];
    theBody->center.x = buf->center_x;
    theBody->center.y = buf->center_y;
    theBody->center.z = buf->center_z;
    theBody->center.w = buf->center_w;
    theBody->mass = buf->mass;
    theBody->linearDamping = buf->linearDamping;
    theBody->angularDamping = buf->angularDamping;
    theBody->timeFactor = buf->timeFactor;
    theBody->gravityFactor = buf->gravityFactor;
    theBody->friction = buf->friction;
    theBody->rollingFrictionMult = buf->rollingFrictionMult;
    theBody->restitution = buf->restitution;
    theBody->maxLinearVelocity = buf->maxLinearVelocity;
    theBody->maxAngularVelocity = buf->maxAngularVelocity;
    theBody->penetrationDepth = buf->penetrationDepth;
    theBody->motionSystem = buf->motionSystem;
    theBody->deactivatorType = buf->deactivatorType;
    theBody->solverDeactivation = buf->solverDeactivation;
    theBody->qualityType = buf->qualityType;
    theBody->autoRemoveLevel = buf->autoRemoveLevel;
    theBody->responseModifierFlag = buf->responseModifierFlag;
    theBody->numShapeKeysInContactPointProps = buf->numShapeKeysInContactPointProps;
    theBody->forceCollideOntoPpu = buf->forceCollideOntoPpu;
    theBody->bodyFlagsInt = buf->bodyFlagsInt;
    theBody->bodyFlags = buf->bodyFlags;
    theBody->shapeRef.index = collShapeIndex;
    int newid = nif->GetHeader().AddBlock(std::move(theBody));

    return newid;
};

NIFLY_API void* getCollTarget(void* nifref, void* node) {
    NifFile* nif = static_cast<NifFile*>(nifref);
    NiHeader hdr = nif->GetHeader();
    nifly::bhkCollisionObject* theNode = static_cast<nifly::bhkCollisionObject*>(node);
    if (theNode)
        return hdr.GetBlock(theNode->targetRef);
    else
        return nullptr;
}

NIFLY_API int getCollFlags(void* node) {
    nifly::bhkCollisionObject* theNode = static_cast<nifly::bhkCollisionObject*>(node);
    if (theNode) {
        return theNode->flags;
    }
    else
        return 0;
}

NIFLY_API int getCollBodyBlockname(void* nifref, int nodeIndex, char* buf, int buflen) {
    NifFile* nif = static_cast<NifFile*>(nifref);
    NiHeader hdr = nif->GetHeader();
    nifly::bhkRigidBody* theBody = hdr.GetBlock<bhkRigidBody>(nodeIndex);

    if (theBody) {
        std::string name = theBody->GetBlockName();
        int copylen = std::min((int)buflen - 1, (int)name.length());
        name.copy(buf, copylen, 0);
        buf[copylen] = '\0';
        return int(name.length());
    }
    else
        return 0;
}

NIFLY_API int getRigidBodyProps(void* nifref, int nodeIndex, BHKRigidBodyBuf* buf)
/*
    Return the rigid body details. Return value = 1 if the node is a rigid body, 0 if not 
    */
{
    NifFile* nif = static_cast<NifFile*>(nifref);
    NiHeader hdr = nif->GetHeader();
    nifly::bhkWorldObject* theWO = hdr.GetBlock<bhkWorldObject>(nodeIndex);
    nifly::bhkRigidBody* theBody = hdr.GetBlock<bhkRigidBody>(nodeIndex);

    if (theWO) {
        buf->collisionFilter_layer = theWO->collisionFilter.layer;
        buf->collisionFilter_flags = theWO->collisionFilter.flagsAndParts;
        buf->collisionFilter_group = theWO->collisionFilter.group;
        buf->broadPhaseType = theWO->broadPhaseType;
        buf->prop_data = theWO->prop.data;
        buf->prop_size = theWO->prop.size;
        buf->prop_flags = theWO->prop.capacityAndFlags;
    }
    if (theBody) {
        buf->collisionResponse = theBody->collisionResponse;
        buf->processContactCallbackDelay = theBody->processContactCallbackDelay;
        buf->collisionFilterCopy_layer = theBody->collisionFilterCopy.layer;
        buf->collisionFilterCopy_flags = theBody->collisionFilterCopy.flagsAndParts;
        buf->collisionFilterCopy_group = theBody->collisionFilterCopy.group;
        buf->translation_x = theBody->translation.x;
        buf->translation_y = theBody->translation.y;
        buf->translation_z = theBody->translation.z;
        buf->translation_w = theBody->translation.w;
        buf->rotation_x = theBody->rotation.x;
        buf->rotation_y = theBody->rotation.y;
        buf->rotation_z = theBody->rotation.z;
        buf->rotation_w = theBody->rotation.w;
        buf->linearVelocity_x = theBody->linearVelocity.x;
        buf->linearVelocity_y = theBody->linearVelocity.y;
        buf->linearVelocity_z = theBody->linearVelocity.z;
        buf->linearVelocity_w = theBody->linearVelocity.w;
        buf->angularVelocity_x = theBody->angularVelocity.x;
        buf->angularVelocity_y = theBody->angularVelocity.y;
        buf->angularVelocity_z = theBody->angularVelocity.z;
        buf->angularVelocity_w = theBody->angularVelocity.w;
        for (int i=0; i < 12; i++) buf->inertiaMatrix[i] = theBody->inertiaMatrix[i];
        buf->center_x = theBody->center.x;
        buf->center_y = theBody->center.y;
        buf->center_z = theBody->center.z;
        buf->center_w = theBody->center.w;
        buf->mass = theBody->mass;
        buf->linearDamping = theBody->linearDamping;
        buf->angularDamping = theBody->angularDamping;
        buf->timeFactor = theBody->timeFactor;
        buf->gravityFactor = theBody->gravityFactor;
        buf->friction = theBody->friction;
        buf->rollingFrictionMult = theBody->rollingFrictionMult;
        buf->restitution = theBody->restitution;
        buf->maxLinearVelocity = theBody->maxLinearVelocity;
        buf->maxAngularVelocity = theBody->maxAngularVelocity;
        buf->penetrationDepth = theBody->penetrationDepth;
        buf->motionSystem = theBody->motionSystem;
        buf->deactivatorType = theBody->deactivatorType;
        buf->solverDeactivation = theBody->solverDeactivation;
        buf->qualityType = theBody->qualityType;
        buf->autoRemoveLevel = theBody->autoRemoveLevel;
        buf->responseModifierFlag = theBody->responseModifierFlag;
        buf->numShapeKeysInContactPointProps = theBody->numShapeKeysInContactPointProps;
        buf->forceCollideOntoPpu = theBody->forceCollideOntoPpu;
        buf->bodyFlagsInt = theBody->bodyFlagsInt;
        buf->bodyFlags = theBody->bodyFlags;
        return 1;
    }
    else
        return 0;
}

NIFLY_API int getRigidBodyShapeID(void* nifref, int nodeIndex) {
    /* Returns the block index of the collision shape */
    NifFile* nif = static_cast<NifFile*>(nifref);
    NiHeader hdr = nif->GetHeader();
    nifly::bhkRigidBody* theBody = hdr.GetBlock<bhkRigidBody>(nodeIndex);
    if (theBody)
        return theBody->shapeRef.index;
    else
        return -1;
}

NIFLY_API int getCollShapeBlockname(void* nifref, int nodeIndex, char* buf, int buflen) {
    NifFile* nif = static_cast<NifFile*>(nifref);
    NiHeader hdr = nif->GetHeader();
    nifly::bhkShape* theBody = hdr.GetBlock<bhkShape>(nodeIndex);

    if (theBody) {
        std::string name = theBody->GetBlockName();
        int copylen = std::min((int)buflen - 1, (int)name.length());
        name.copy(buf, copylen, 0);
        buf[copylen] = '\0';
        return int(name.length());
    }
    else
        return 0;
}

NIFLY_API int getCollConvexVertsShapeProps(void* nifref, int nodeIndex, BHKConvexVertsShapeBuf* buf)
/*
    Return the collision shape details. Return value = 1 if the node is a known collision shape,
    0 if not
    */
{
    NifFile* nif = static_cast<NifFile*>(nifref);
    NiHeader hdr = nif->GetHeader();
    nifly::bhkConvexVerticesShape* sh = hdr.GetBlock<bhkConvexVerticesShape>(nodeIndex);

    if (sh) {
        buf->material = sh->GetMaterial();
        buf->radius = sh->radius;
        buf->vertsProp_data = sh->vertsProp.data;
        buf->vertsProp_size = sh->vertsProp.size;
        buf->vertsProp_flags = sh->vertsProp.capacityAndFlags;
        buf->normalsProp_data = sh->normalsProp.data;
        buf->normalsProp_size = sh->normalsProp.size;
        buf->normalsProp_flags = sh->normalsProp.capacityAndFlags;
        return 1;
    }
    else
        return 0;
};

NIFLY_API int addCollConvexVertsShape(void* nifref, const BHKConvexVertsShapeBuf* buf, 
        float* verts, int vertcount, float* normals, int normcount) {
    NifFile* nif = static_cast<NifFile*>(nifref);
    NiHeader hdr = nif->GetHeader();

    auto sh = std::make_unique<bhkConvexVerticesShape>();
    sh->SetMaterial(buf->material);
    sh->radius = buf->radius;
    for (int i = 0; i < vertcount * 4; i += 4) {
        Vector4 v = Vector4(verts[i], verts[i + 1], verts[i + 2], verts[i + 3]);
        sh->verts.push_back(v);
    };
    for (int i = 0; i < normcount * 4; i += 4) {
        Vector4 n = Vector4(normals[i], normals[i + 1], normals[i + 2], normals[i + 3]);
        sh->normals.push_back(n);
    }
    int newid = nif->GetHeader().AddBlock(std::move(sh));
    return newid;
};

NIFLY_API int getCollShapeVerts(void* nifref, int nodeIndex, float* buf, int buflen)
/*
    Return the collision shape vertices. Return number of vertices in shape. *buf may be null.
    buflen = number of verts the buffer can receive, so buf must be 4x this size.
    */
{
    NifFile* nif = static_cast<NifFile*>(nifref);
    NiHeader hdr = nif->GetHeader();
    int vertCount = 0;

    nifly::bhkConvexVerticesShape* sh = hdr.GetBlock<bhkConvexVerticesShape>(nodeIndex);

    if (sh) {
        vertCount = sh->verts.size();
        for (int i = 0, j = 0; i < vertCount && j < buflen*4; i++) {
            buf[j++] = sh->verts[i].x;
            buf[j++] = sh->verts[i].y;
            buf[j++] = sh->verts[i].z;
            buf[j++] = sh->verts[i].w;
        };
        return vertCount;
    }
    else
        return 0;
}

NIFLY_API int getCollShapeNormals(void* nifref, int nodeIndex, float* buf, int buflen)
/*
    Return the collision shape vertices. Return number of vertices in shape. *buf may be null.
    buflen = number of verts the buffer can receive, so buf must be 4x this size.
    */
{
    NifFile* nif = static_cast<NifFile*>(nifref);
    NiHeader hdr = nif->GetHeader();
    int vertCount = 0;

    nifly::bhkConvexVerticesShape* sh = hdr.GetBlock<bhkConvexVerticesShape>(nodeIndex);

    if (sh) {
        vertCount = sh->normals.size();
        for (int i = 0, j = 0; i < vertCount && j < buflen*4; i++) {
            buf[j++] = sh->normals[i].x;
            buf[j++] = sh->normals[i].y;
            buf[j++] = sh->normals[i].z;
            buf[j++] = sh->normals[i].w;
        };
        return vertCount;
    }
    else
        return 0;
}

NIFLY_API int getCollBoxShapeProps(void* nifref, int nodeIndex, BHKBoxShapeBuf* buf)
/*
    Return the collision shape details. Return value = 1 if the node is a known collision shape, 
    0 if not
    */
{
    NifFile* nif = static_cast<NifFile*>(nifref);
    NiHeader hdr = nif->GetHeader();
    nifly::bhkBoxShape* sh = hdr.GetBlock<bhkBoxShape>(nodeIndex);

    if (sh) {
        buf->material = sh->GetMaterial();
        buf->radius = sh->radius;
        buf->dimensions_x = sh->dimensions.x;
        buf->dimensions_y = sh->dimensions.y;
        buf->dimensions_z = sh->dimensions.z;
        return 1;
    }
    else
        return 0;
}

NIFLY_API int addCollBoxShape(void* nifref, const BHKBoxShapeBuf* buf) {
    NifFile* nif = static_cast<NifFile*>(nifref);
    NiHeader hdr = nif->GetHeader();

    auto sh = std::make_unique<bhkBoxShape>();
    sh->SetMaterial(buf->material);
    sh->radius = buf->radius;
    sh->dimensions.x = buf->dimensions_x;
    sh->dimensions.y = buf->dimensions_y;
    sh->dimensions.z = buf->dimensions_z;
    int newid = nif->GetHeader().AddBlock(std::move(sh));
    return newid;
};

NIFLY_API int getCollListShapeProps(void* nifref, int nodeIndex, BHKListShapeBuf* buf)
/*
    Return the collision shape details. Return value = 1 if the node is a known collision shape,
    0 if not
    */
{
    NifFile* nif = static_cast<NifFile*>(nifref);
    NiHeader hdr = nif->GetHeader();
    nifly::bhkListShape* sh = hdr.GetBlock<bhkListShape>(nodeIndex);

    if (sh) {
        buf->material = sh->GetMaterial();
        buf->childShape_data = sh->childShapeProp.data;
        buf->childShape_size = sh->childShapeProp.size;
        buf->childShape_flags = sh->childShapeProp.capacityAndFlags;
        buf->childFilter_data = sh->childFilterProp.data;
        buf->childFilter_size = sh->childFilterProp.size;
        buf->childFilter_flags = sh->childFilterProp.capacityAndFlags;
        return 1;
    }
    else
        return 0;
}

NIFLY_API int getCollListShapeChildren(void* nifref, int nodeIndex, uint32_t* buf, int buflen)
/*
    Return the collision shape children.
    */
{
    NifFile* nif = static_cast<NifFile*>(nifref);
    NiHeader hdr = nif->GetHeader();
    int childCount = 0;

    nifly::bhkListShape* sh = hdr.GetBlock<bhkListShape>(nodeIndex);

    if (sh) {
        std::vector<uint32_t> children;
        sh->GetChildIndices(children);
        childCount = children.size();
        for (int i = 0; i < childCount && i < buflen; i++) {
            buf[i] = children[i];
        };
        return childCount;
    }
    else
        return 0;
}

NIFLY_API int addCollListShape(void* nifref, const BHKListShapeBuf* buf) {
    NifFile* nif = static_cast<NifFile*>(nifref);
    NiHeader hdr = nif->GetHeader();

    auto sh = std::make_unique<bhkListShape>();
    sh->SetMaterial(buf->material);
    sh->childShapeProp.data = buf->childShape_data;
    sh->childShapeProp.size = buf->childShape_size;
    sh->childShapeProp.capacityAndFlags = buf->childShape_flags;
    sh->childFilterProp.data = buf->childFilter_data;
    sh->childFilterProp.size = buf->childFilter_size;
    sh->childFilterProp.capacityAndFlags = buf->childFilter_flags;
    int newid = nif->GetHeader().AddBlock(std::move(sh));
    return newid;
};

NIFLY_API void addCollListChild(void* nifref, const uint32_t id, uint32_t child_id) {
    NifFile* nif = static_cast<NifFile*>(nifref);
    NiHeader hdr = nif->GetHeader();
    bhkListShape* collList = hdr.GetBlock<bhkListShape>(id);

   collList->subShapeRefs.AddBlockRef(child_id);
};

NIFLY_API int getCollConvexTransformShapeProps(
    void* nifref, int nodeIndex, BHKConvexTransformShapeBuf* buf)
/*
    Return the collision shape details. Return value = 1 if the node is a known collision shape,
    0 if not
    */
{
    NifFile* nif = static_cast<NifFile*>(nifref);
    NiHeader hdr = nif->GetHeader();
    nifly::bhkConvexTransformShape* sh = hdr.GetBlock<bhkConvexTransformShape>(nodeIndex);

    if (sh) {
        buf->material = sh->material;
        buf->radius = sh->radius;
        for (int i = 0; i < 16; i++) {
            buf->xform[i] = sh->xform[i]; 
        };
        return 1;
    }
    else
        return 0;
}

NIFLY_API int getCollConvexTransformShapeChildID(void* nifref, int nodeIndex) {
    /* Returns the block index of the collision shape */
    NifFile* nif = static_cast<NifFile*>(nifref);
    NiHeader hdr = nif->GetHeader();
    nifly::bhkConvexTransformShape* sh = hdr.GetBlock<bhkConvexTransformShape>(nodeIndex);
    if (sh)
        return sh->shapeRef.index;
    else
        return -1;
}

NIFLY_API int addCollConvexTransformShape(void* nifref, const BHKConvexTransformShapeBuf* buf) {
    NifFile* nif = static_cast<NifFile*>(nifref);
    NiHeader hdr = nif->GetHeader();

    auto sh = std::make_unique<bhkConvexTransformShape>();
    sh->SetMaterial(buf->material);
    sh->radius = buf->radius;
    for (int i = 0; i < 16; i++) {
        sh->xform[i] = buf->xform[i];
    };

    int newid = nif->GetHeader().AddBlock(std::move(sh));
    return newid;
};

NIFLY_API void setCollConvexTransformShapeChild(
        void* nifref, const uint32_t id, uint32_t child_id) {
    NifFile* nif = static_cast<NifFile*>(nifref);
    NiHeader hdr = nif->GetHeader();
    bhkConvexTransformShape* cts = hdr.GetBlock<bhkConvexTransformShape>(id);

    cts->shapeRef.index = child_id;
};

NIFLY_API int getCollCapsuleShapeProps(void* nifref, int nodeIndex, BHKCapsuleShapeBuf* buf)
/*
    Return the collision shape details. Return value = 1 if the node is a known collision shape,
    0 if not
    */
{
    NifFile* nif = static_cast<NifFile*>(nifref);
    NiHeader hdr = nif->GetHeader();
    nifly::bhkCapsuleShape* sh = hdr.GetBlock<bhkCapsuleShape>(nodeIndex);

    if (sh) {
        buf->material = sh->GetMaterial();
        buf->radius = sh->radius;
        buf->radius1 = sh->radius1;
        buf->radius2 = sh->radius2;
        for (int i = 0; i < 3; i++) buf->point1[i] = sh->point1[i];
        for (int i = 0; i < 3; i++) buf->point2[i] = sh->point2[i];
        return 1;
    }
    else
        return 0;
}

NIFLY_API int addCollCapsuleShape(void* nifref, const BHKCapsuleShapeBuf* buf) {
    NifFile* nif = static_cast<NifFile*>(nifref);
    NiHeader hdr = nif->GetHeader();

    auto sh = std::make_unique<bhkCapsuleShape>();
    sh->SetMaterial(buf->material);
    sh->radius = buf->radius;
    sh->radius1 = buf->radius1;
    sh->radius2 = buf->radius2;
    for (int i = 0; i < 3; i++) sh->point1[i] = buf->point1[i];
    for (int i = 0; i < 3; i++) sh->point2[i] = buf->point2[i];
    
    int newid = nif->GetHeader().AddBlock(std::move(sh));
    return newid;
};

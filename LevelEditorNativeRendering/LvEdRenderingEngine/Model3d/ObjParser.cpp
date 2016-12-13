#include "../Core/Utils.h"
#include <string>
#include <algorithm>
#include <string>
#include <vector>
#include <map>
#include <cctype>
#include <functional>
#include "../Core/Logger.h"
#include "../Core/FileUtils.h"
#include "../ResourceManager/ResourceManager.h"
#include "Model3dBuilder.h"
#include "ObjModelFactory.h"

#include "ObjParser.h"


using namespace champ;

//////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////
struct Group
{
    size_t m_startFace;
    size_t m_faceCount;
    std::string m_usemtl;
};

static inline bool IsBlank(char c)
{
    return c == ' ' || c == '\t' || c == '\r';
}

static inline size_t FindNextBlank(const std::string& str, size_t ndx)
{
    size_t i = ndx;
    while (i < str.length() && !IsBlank(str[i]))
        ++i;
    return i;
}

static inline size_t ConsumeBlanks(const std::string& str, size_t ndx)
{
    size_t i = ndx;
    while (i < str.length() && IsBlank(str[i]))
        ++i;
    return i;
}

template<int N, typename V>
inline void ParseFloats(const std::string& str, V& out, size_t idx)
{
    std::string subs;
    size_t lastIdx = std::string::npos;
    float* xyz = &out.x;
    for (int i = 0; i < N; ++i)
    {
        idx = ConsumeBlanks(str, idx);
        if (idx >= str.length()) break;
        lastIdx = idx;
        idx = FindNextBlank(str, idx);
        if (lastIdx == idx) break;
        subs = str.substr(lastIdx, idx-lastIdx);
        xyz[i] = (float)::atof(subs.c_str());
    }
}

inline void ParseInts3(std::string& str, int32_t* outValues)
{
    size_t i[3];
    i[0] = 0; 
    size_t _i = 1;
    size_t count = str.length();
    for ( size_t j = 0; j < count; ++j)
    { 
        if (str[j] == '/' || str[j] == ' ')
        {
            str[j] = 0;            
            i[_i++] = j + 1;
        }
    }

    for (size_t j = 0; j < 3; ++j)
    {
        if (!*(str.c_str() + i[j]))
            outValues[j] = INT32_MAX;
        else
            outValues[j] = ::atoi(str.c_str() + i[j]) - 1;
    }
}

template<typename T3, typename T2>
static inline bool ParseFace(const std::string& line, size_t idx, const T3& pos, const T3& nor, const T2& uv, std::vector<ObjParser::Vertex>& outVertices )
{
    ObjParser::Vertex v;
    int32_t indices[3];     // v, vt, vn
    size_t lastNdx;
    size_t count = 3;
    for (int i = 0; i < count; ++i) // at least three vertices
    {
        idx = ConsumeBlanks(line, idx);
        if (idx >= line.length()) break;
        lastNdx = idx;
        idx = FindNextBlank(line, idx);
        std::string subs = line.substr(lastNdx, idx - lastNdx);
        ParseInts3(subs, indices);
        
        if (indices[0] < 0) indices[0] = (int32_t)pos.size() + indices[0] + 1;
        if (indices[1] < 0) indices[1] = (int32_t)uv.size() + indices[1] + 1;
        if (indices[2] < 0) indices[2] = (int32_t)nor.size() + indices[2] + 1;

        if (indices[0] < pos.size() && indices[2] < nor.size())
        {
            v.pos = pos[indices[0]];
            if (indices[1] >= 0 && indices[1] < uv.size())
                v.uv = uv[indices[1]];
            else
                v.uv.x = v.uv.y = 0.0f;
            v.normal = nor[indices[2]];
            if (i == 3) // it's a quad, triangulate fan
            {
                if (outVertices.size() < 3) 
                    break; // some index was wrong
                
                outVertices.push_back(outVertices[0]);
                outVertices.push_back(outVertices[2]);
            }
            outVertices.push_back(v);
        }
        if (i >= 2 && idx < line.length())
            ++count;
    }
    const size_t cv = outVertices.size();
    return cv == 3 || cv == 6;
}

// trim from start
inline std::string &StrLtrim(std::string &s)
{
    s.erase(s.begin(), std::find_if(s.begin(), s.end(),
        std::not1(std::ptr_fun<int, int>(std::isspace))));
    return s;
}

// trim from end
inline std::string &StrRtrim(std::string &s)
{
    s.erase(std::find_if(s.rbegin(), s.rend(),
        std::not1(std::ptr_fun<int, int>(std::isspace))).base(), s.end());
    return s;
}

// trim from both ends
inline std::string &StrTrim(std::string &s)
{
    return StrLtrim(StrRtrim(s));
}

inline bool GetLine(const std::vector<byte>& data, size_t& cur, std::string& outLine)
{
    if (cur >= data.size()) return false;
    outLine.clear();
    char c;
    while (cur < data.size() && (c = data[cur]) != '\n')
    {
        outLine += c;
        ++cur;
    }

    if (c == '\n')
        ++cur;

    StrTrim(outLine);
    return cur <= data.size();
}

ObjParser::ObjParser(const std::vector<byte>& data)
{
    if (data.empty()) return;
    std::vector<Group> groups;
    std::vector<v3> positions;
    std::vector<v3> normals;
    std::vector<v2> texCoords;
    std::vector<Vertex> tempVertices; 
    tempVertices.reserve(6);
    size_t cur = 0;
    std::string line; 
    line.reserve(512);
    v3 _v3;
    v2 _v2;
    bool doneRecenter = false;
    while ( GetLine(data, cur, line) )
    {
        if (line.empty()) continue;
        switch (*line.begin())
        {
        case '#':
            if (line.find_first_of("NORECENTER", 0) != std::string::npos)
                doneRecenter = true;
            break;
        case 'v': 
            switch ( *(line.begin()+1)) 
            {
            case ' ': 
                ParseFloats<3, v3>(line, _v3, 2);
                positions.push_back(_v3);
                break;
            case 'n': 
                ParseFloats<3, v3>(line, _v3, 3);
                normals.push_back(_v3);
                break;
            case 't': 
                ParseFloats<2, v2>(line, _v2, 3);
                _v2.y = 1.0f - _v2.y;
                texCoords.push_back(_v2);
                break;
            }
            break;
        
        case 'm': 
            m_materialLib = line.substr(7);
            break;

        case 'u':
        {
            auto v = line.substr(7);
            std::transform(v.begin(), v.end(), v.begin(), ::towlower);
            m_objects.back().m_material = v;
        }break;
        
        case 'f': 
        {
            // recenter
            if (!doneRecenter)
            {
                if (!positions.empty())
                {
                    v3 com = { 0 };
                    for (const auto& p : positions)
                    {
                        com.x += p.x;
                        com.y += p.y;
                        com.z += p.z;
                    }
                    const float invSize = 1.0f / positions.size();
                    com.x *= invSize; com.y *= invSize; com.z *= invSize;

                    for (auto& p : positions)
                    {
                        p.x -= com.x;
                        p.y -= com.y;
                        p.z -= com.z;
                    }
                }
                doneRecenter = true;
            }

            tempVertices.clear();
            if (ParseFace(line, 2, positions, normals, texCoords, tempVertices))
            {
                auto& v = m_objects.back().m_vertexBuffer;
                v.insert(v.end(), tempVertices.begin(), tempVertices.end());
            }
        }break;

        case 'g':
        case 'o':
        {
            SubObj obj;
            obj.m_name = line.substr(2);
            m_objects.push_back(obj);
        }break;
        }
    }

    // prepare IBs (very simple, it's an expanded triangle soup)
    for (auto& o : m_objects)
    {
#if defined(_DEBUG) && defined(FORCE_16BIT_INDICES)
        if (o.m_vertexBuffer.size() >= (1 << 16))
            throw Platform::Exception::CreateException(E_FAIL, L"This OBJ needs 32 bits IB. Only 16 bits supported. Will be truncated in RELEASE");
#endif

#ifdef FORCE_16BIT_INDICES
        const size_t maxVerts = std::min(size_t((1 << 16) - 1), o.m_vertexBuffer.size());
#else
        const size_t maxVerts = o.m_vertexBuffer.size();
#endif
        auto& ib = o.m_indexBuffer;
        ib.reserve(maxVerts);
        for (size_t i = 0; i < maxVerts; ++i)
            ib.push_back((SubObj::IndexType)i);
    }
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////
enum mttoken
{
    MT_UNKNOWN = 0,
    MT_name,
    MT_map_ka,
    MT_map_ks,
    MT_map_kd,
    MT_map_d,
    MT_map_ns,
    MT_map_bump,
    MT_map_disp,
    MT_map_decal,
    MT_ka,
    MT_kd,
    MT_ks,
    MT_tr,
    MT_tf,
    MT_ns,
    MT_sharpness,
    MT_ni,
    MT_illum
};

#define cmptok(t,n) if(_strnicmp(w.c_str(),#t,n)==0) tok=MT_##t
mttoken mtl_tokenize(const std::string& w, int* m)
{
    mttoken tok = MT_UNKNOWN;
    switch (w.length())
    {
    case 1: // d 
        if (*w.begin() == 'd') tok = MT_tr;
        break;
    case 2: // ka kd ks tr ns ni tf
        cmptok(ka, 2);
        else cmptok(kd, 2);
        else cmptok(ks, 2);
        else cmptok(tr, 2);
        else cmptok(ns, 2);
        else cmptok(ni, 2);
        else cmptok(tf, 2);
    break;
    case 4: // bump disp
        if (_strnicmp(w.c_str(), "bump", 4) == 0) tok = MT_map_bump;
        else if (_strnicmp(w.c_str(), "disp", 4) == 0) tok = MT_map_disp;
        break;
    case 5: // illum map_d decal
        cmptok(illum, 5);
        else cmptok(map_d, 5);
    else if (_strnicmp(w.c_str(), "decal", 5) == 0) tok = MT_map_decal;
    break;
    case 6: // map_ka map_ks map_kd newmtl
        if (_strnicmp(w.c_str(), "newmtl", 6) == 0) { ++*m; tok = MT_name; }
        else cmptok(map_ka, 6);
    else cmptok(map_ks, 6);
    else cmptok(map_kd, 6);
    break;
    case 8: // map_bump
        cmptok(map_bump, 8);
        break;
    case 9: // sharpness
        cmptok(sharpness, 9);
        break;
    }
    return tok;
}

#define EMITPARSETUPLECODE(typ,f) \
void parse##typ(const std::string& b, int n, void* data)\
{\
  typ* ptr=(typ*)data;\
  switch(n)\
  {\
  case 1: sscanf_s(b.c_str(),f,ptr); break;\
  case 2: sscanf_s(b.c_str(),f f,ptr,ptr+1); break;\
  case 3: sscanf_s(b.c_str(),f f f,ptr,ptr+1,ptr+2); break;\
  }\
}

EMITPARSETUPLECODE(float, "%f ");
EMITPARSETUPLECODE(int, "%d ");
#define TOKEN_STR(n) case MT_##n: curMat->n = v; break
#define TOKEN_3FL(n) case MT_##n: parsefloat(v, 3, &curMat->n); break
#define TOKEN_1FL(n) case MT_##n: parsefloat(v, 1, &curMat->n); break
#define TOKEN_1IN(n) case MT_##n: parseint  (v, 1, &curMat->n); break


void MtlParser::Parse(const std::vector<byte>& data)
{
    size_t cur = 0;
    std::string line, w, v;
    Material* curMat = nullptr;
    while (GetLine(data, cur, line))
    {
        if (line.empty()) continue;
        if (*line.begin() == '#') continue;
        size_t s = ConsumeBlanks(line, 0);
        size_t b = FindNextBlank(line, s);
        w = line.substr(s, b-s);
        v = StrTrim(line.substr(b));
        int nmaps = 0;
        switch (mtl_tokenize(w, &nmaps))
        {
            case MT_name: 
            {
                if (curMat)
                    m_materials[curMat->name] = curMat;
                curMat = new Material();
                std::transform(v.begin(), v.end(), v.begin(), ::towlower);
                curMat->name = v;
            }
            TOKEN_STR(map_ka);
            TOKEN_STR(map_ks);
            TOKEN_STR(map_kd);
            TOKEN_STR(map_d);
            TOKEN_STR(map_ns);
            TOKEN_STR(map_bump);
            TOKEN_STR(map_disp);
            TOKEN_STR(map_decal);
            TOKEN_3FL(tf);
            TOKEN_3FL(ka);
            TOKEN_3FL(kd);
            TOKEN_3FL(ks);
            TOKEN_1FL(tr);
            TOKEN_1FL(ns);
            TOKEN_1FL(sharpness);
            TOKEN_1FL(ni);
            TOKEN_1IN(illum);
        }
    }
    if (curMat)
        m_materials[curMat->name] = std::move(curMat);
}


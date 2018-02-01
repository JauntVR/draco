/*
* @file psy_draco.h
*
* Copyright (c) 2017 Personify Inc.
*
* @brief
*
*/

#ifndef PSY_DRACO_COMMON_H
#define PSY_DRACO_COMMON_H

#if defined (WIN32)
#    ifdef PSY_DRACO_STATIC_LIB
#        define PSY_DRACO_API
#    else
#        ifdef PSY_DRACO_EXPORT
#            define PSY_DRACO_API __declspec(dllexport)
#        else
#            define PSY_DRACO_API __declspec(dllimport)
#        endif
#    endif
#else
#    ifdef PSY_DRACO_EXPORT
#        define PSY_DRACO_API __attribute__((visibility("default")))
#    else
#        define PSY_DRACO_API
#    endif
#endif

#include <memory>
class PSY_DRACO_API IProfiler {};
class PSY_DRACO_API IProfilerManager
{
public:
    virtual std::shared_ptr<IProfiler> CreateProfilerSection(const char* pName) = 0;
    virtual ~IProfilerManager() {}
};

namespace psy
{

PSY_DRACO_API IProfilerManager* GetProfilerManager();
PSY_DRACO_API void SetProfilerManager(IProfilerManager*);

namespace draco
{

enum PSY_DRACO_API MeshType : uint8_t
{
    FULL_MESH = 0,
    INCREMENTAL_MESH = 1
};

/*
 * change logs
 * - 1.0: support incremental mesh compression
 * - 1.1: 2018/01/05 (*)
 *     + support vertex color compression
 */
#define PSY_DRACO_API_MAJOR_VERSION 1
#define PSY_DRACO_API_MINOR_VERSION 1

struct PSY_DRACO_API Header
{
    uint8_t mMajorVersion;
    uint8_t mMinorVersion;
    float mDecodeMultiplier;
    MeshType mMeshType;
    uint32_t mIFrameIndex;
};

}; // namespace draco
}; // namespace psy

#ifndef PSY_DRACO_PROFILE_ENABLE
#define PSY_DRACO_PROFILE_ENABLE 1
#endif

#if PSY_DRACO_PROFILE_ENABLE
#define PSY_DRACO_PROFILE_SECTION(name) \
        IProfilerManager* prof_manager = psy::GetProfilerManager(); \
        std::shared_ptr<IProfiler> psy_draco_prof_section = \
            ((prof_manager) ? (prof_manager->CreateProfilerSection(name)) : (nullptr));
#else
#define PSY_DRACO_PROFILE_SECTION(name)
#endif // PSY_DRACO_PROFILE_ENABLE

#endif // PSY_DRACO_COMMON_H

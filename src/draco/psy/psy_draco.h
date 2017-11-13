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
};

namespace psy
{
    PSY_DRACO_API IProfilerManager* GetProfilerManager();
    PSY_DRACO_API void SetProfilerManager(IProfilerManager*);
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

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

#endif // PSY_DRACO_COMMON_H

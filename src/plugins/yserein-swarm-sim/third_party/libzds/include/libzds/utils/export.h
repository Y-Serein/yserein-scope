//
// Created by Hoppz on 25-12-8.
//

#ifndef LIBZDS_UTILS_EXPORT_H
#define LIBZDS_UTILS_EXPORT_H

    #if !defined(LIBZDS_EXPORT)
        #if defined(LIBZDS_SHARED_LIBRARY)
            #if defined(_WIN32)
                #if defined(LIBZDS_COMPILE_LIBRARY)
                    #define LIBZDS_EXPORT __declspec(dllexport)
                #else
                    #define LIBZDS_EXPORT __declspec(dllimport)
                #endif    // defined(LEVELED_COMPILE_LIBRARY)
            #else     // defined(_WIN32)
                #if defined(LIBZDS_COMPILE_LIBRARY)
                    #define LIBZDS_EXPORT __attribute__((visibility("default")))
                #else
                    #define LIBZDS_EXPORT
                #endif
            #endif    // defined(_WIN32)
        #else     // defined(LIBZDS_SHARED_LIBRARY)
            #define LIBZDS_EXPORT
        #endif
    #endif    //! defined(LIBZDS_EXPORT)

#endif //LIBZDS_UTILS_EXPORT_H

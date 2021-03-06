//
//  utils.h
//  opencv_playground
//
//  Created by Lincoln Hard on 2017/2/2.
//  Copyright © 2017年 Lincoln Hard. All rights reserved.
//

#ifndef utils_hpp
#define utils_hpp

#ifdef __cplusplus
extern "C" {
#endif

#include <time.h> //for srand
#include <limits.h> //for USHRT_MAX
#include <stdio.h> //for printf
#include <stdlib.h> //for aligned malloc
#include <string.h> //for memset
#include <stdbool.h> //for bool

#define GCT_STACK_TOTAL_SIZE (0xB00000) //14Mbytes
#define GCT_STACK_RESERVED_SIZE (0x300000) //for three gray image one hsv image
#define GCT_IMG_WIDTH (1280) //video width
#define GCT_IMG_HEIGHT (720) //video height
#define GCT_THETA_NORMALIZED_RANGE (360)
#define GCT_TIME_NORMALIZED_RANGE (1000) //1000 frame is big enough, should relate with recording program

#define CV_ABS( x ) ( ( x ) >= 0 ? ( x ) : -( x ) )
#define CV_MAX( a, b ) ( ( a ) > ( b ) ? ( a ) : ( b ) )
#define CV_MIN( a, b ) ( ( a ) < ( b ) ? ( a ) : ( b ) )
#define CV_POW2( x ) ( ( x ) * ( x ) )


typedef unsigned short GCT_FRAME_INDEX; //note: uchar for 60fps video, ushrt for higher

typedef struct
    {
    void* stack_starting_address;
    char* stack_current_address;
    unsigned int stack_current_alloc_size;
    }GCT_stack;

typedef struct
    {
    unsigned char a;
    unsigned char b;
    }GCT_2uchar;

typedef struct
    {
    union
        {
        unsigned short x;
        unsigned short t;
        };
    union
        {
        unsigned short y;
        unsigned short r;
        };
    }GCT_point2ushort;

typedef struct
    {
    double x;
    double y;
    double z;
    }GCT_point3double;

void gct_init_stack
    (
    void
    );

void gct_free_stack
    (
    void
    );

void* gct_alloc_from_stack
    (
    unsigned int len
    );

void gct_partial_free_from_stack
    (
    unsigned int len
    );

void gct_reset_stack_ptr_to_initial_position
    (
    void
    );

void gct_reset_stack_ptr_to_unreserved_position
    (
    void
    );

void gct_reset_stack_ptr_to_assigned_position
    (
    unsigned int assigned_size
    );

unsigned int gct_get_stack_current_alloc_size
    (
    void
    );

double* gct_alloc_vector
    (
    unsigned int len
    );

void gct_free_vector
    (
    unsigned int len
    );

void gct_print_vector
    (
    double* V,
    unsigned int len
    );

double** gct_alloc_matrix
    (
    unsigned int nrow,
    unsigned int ncol
    );

void gct_free_matrix
    (
    unsigned int nrow,
    unsigned int ncol
    );

void gct_print_matrix
    (
    double** M,
    unsigned int nrow,
    unsigned int ncol
    );

#ifdef __cplusplus
}
#endif

#endif /* utils_hpp */

//
//  gct.h
//  opencv_playground
//
//  Created by Lincoln Hard on 2017/2/2.
//  Copyright © 2017年 Lincoln Hard. All rights reserved.
//

#ifndef gct_hpp
#define gct_hpp

#ifdef __cplusplus
extern "C" {
#endif

#include "utils.h"

#define GCT_UPSWING_FITTING_ORDER (4)
#define GCT_DOWNSWING_FITTING_ORDER (6)
#define GCT_UPSWING_TIMING_ORDER (3)
#define GCT_DOWNSWING_TIMING_ORDER (5)
#define GCT_UPFITTING_ITERATION_ROUNDS (100)
#define GCT_DOWNFITTING_ITERATION_ROUNDS (200)

typedef struct
    {
    unsigned int area;
    unsigned int avgx;
    unsigned int avgy;
    unsigned short width;
    unsigned short height;
    }GCT_blob_info;

typedef struct node_struct GCT_linked_list_node;
struct node_struct
    {
    GCT_linked_list_node* next;
    GCT_point2ushort position;
    };

typedef struct
    {
    double** fitting_coeffs;
    double** timing_coeffs;
    }GCT_estimation_result;

typedef struct
    {
    bool is_found_clubhead; //0: not found, 1: found (for debug)
    bool is_downswing; //0: upswing, 1: down swing
    bool is_endnow; //0: swinging, 1: swing finished
    GCT_FRAME_INDEX transition_count;
    GCT_FRAME_INDEX transition_index; //count index
    GCT_FRAME_INDEX current_index; //count index
    GCT_FRAME_INDEX frame_index; //time index
    unsigned int stick_length; //for outlier removal
    GCT_point2ushort* clubhead_pos_carte; //Cartesian coordinate
    GCT_point2ushort* clubhead_pos_polar; //Polar coordinate
    GCT_FRAME_INDEX* clubhead_timing; // time index
    }GCT_swing_state;

typedef struct
    {
    //input
    unsigned char* buffer_gray_previous;
    unsigned char* buffer_gray_current;
    unsigned char* buffer_gray_next;
    //output
    GCT_swing_state* swingstate;
    GCT_estimation_result* upestimation;
    GCT_estimation_result* downestimation;
    GCT_point2ushort* headtrajectory;
    }GCT_work_space_type;

#define AT_PIXEL(I,i,j)	*((I)+(GCT_IMG_WIDTH*(j))+(i))

#define GCT_BYTE_LIMIT(x) ((x)>0xFF?(0xFF):(x))

#define GCT_SVDSIGN(x,y) ((y) >= 0.0 ? fabs(x) : -fabs(x))

#define GCT_FIXED_SIGN(x) ((1) | ((x) >> (sizeof(int)* CHAR_BIT - 1)))

__inline float three_var_max_fl
    (
    float a,
    float b,
    float c
    )
{
    float temp = a;
    (temp < b) && (temp = b);
    (temp < c) && (temp = c);
    return temp;
}

__inline float three_var_min_fl
    (
    float a,
    float b,
    float c
    )
{
    float temp = a;
    (temp > b) && (temp = b);
    (temp > c) && (temp = c);
    return temp;
}

void gct_bgr2gray
    (
    const unsigned char* bgr_src,
    unsigned char* gray_dst
    );

void gct_bgr2hsv
    (
    const unsigned char* bgr_src,
    unsigned char* hsv_dst
    );

void gct_image_transpose
    (
    unsigned char* im,
    int channel_num
    );

void gct_channel_one_image_flip
    (
    unsigned char* im
    );

void gct_image_flip
    (
    unsigned char* im,
    int channel_num
    );

void gct_image_rotate_clockwise
    (
    unsigned char* im,
    int channel_num
    );

void gct_trajectory_preprocess_and_detection
    (
    const GCT_work_space_type* gct_wksp
    );

void gct_fitting
    (
    GCT_work_space_type* gct_wksp
    );

void gct_set_polar_center
    (
    const GCT_point2ushort polarcenter
    );

#ifdef __cplusplus
}
#endif

#endif /* gct_hpp */

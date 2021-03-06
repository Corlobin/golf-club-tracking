//
//  main.cpp
//  opencv_playground
//
//  Created by Lincoln Hard on 2016/11/17.
//  Copyright © 2016年 Lincoln Hard. All rights reserved.
//
#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>
using namespace cv;
#include "utils.h"
#include "gct.h"

static GCT_work_space_type* gct_wksp = NULL;

void usage_help
    (
    void
    )
{
    printf("Usage: golf_club_trajectory.exe [video path] [video start frame position]\n");
}

void GCT_release
    (
    void
    )
{
    gct_free_stack();
}

void GCT_init_2D
    (
    void
    )
{
    gct_init_stack();
    gct_wksp = (GCT_work_space_type*)gct_alloc_from_stack(sizeof(GCT_work_space_type));
    gct_wksp->buffer_gray_previous = (unsigned char*)gct_alloc_from_stack(GCT_IMG_WIDTH * GCT_IMG_HEIGHT * sizeof(unsigned char));
    gct_wksp->buffer_gray_current = (unsigned char*)gct_alloc_from_stack(GCT_IMG_WIDTH * GCT_IMG_HEIGHT * sizeof(unsigned char));
    gct_wksp->buffer_gray_next = (unsigned char*)gct_alloc_from_stack(GCT_IMG_WIDTH * GCT_IMG_HEIGHT * sizeof(unsigned char));
    gct_wksp->swingstate = (GCT_swing_state*)gct_alloc_from_stack(sizeof(GCT_swing_state));
    gct_wksp->swingstate->is_found_clubhead = false;
    gct_wksp->swingstate->is_downswing = false;
    gct_wksp->swingstate->is_endnow = false;
    gct_wksp->swingstate->transition_count = 0;
    gct_wksp->swingstate->transition_index = 0;
    gct_wksp->swingstate->current_index = 0;
    gct_wksp->swingstate->frame_index = 0;
    gct_wksp->swingstate->stick_length = 0;
    gct_wksp->swingstate->clubhead_pos_carte = (GCT_point2ushort*)gct_alloc_from_stack(GCT_TIME_NORMALIZED_RANGE * sizeof(GCT_point2ushort));
    gct_wksp->swingstate->clubhead_pos_polar = (GCT_point2ushort*)gct_alloc_from_stack(GCT_TIME_NORMALIZED_RANGE * sizeof(GCT_point2ushort));
    gct_wksp->swingstate->clubhead_timing = (GCT_FRAME_INDEX*)gct_alloc_from_stack(GCT_TIME_NORMALIZED_RANGE * sizeof(GCT_FRAME_INDEX));
    gct_wksp->upestimation = (GCT_estimation_result*)gct_alloc_from_stack(sizeof(GCT_estimation_result));
    gct_wksp->downestimation = (GCT_estimation_result*)gct_alloc_from_stack(sizeof(GCT_estimation_result));
    gct_wksp->upestimation->fitting_coeffs = gct_alloc_matrix(GCT_UPSWING_FITTING_ORDER + 1, 1);
    gct_wksp->downestimation->fitting_coeffs = gct_alloc_matrix(GCT_DOWNSWING_FITTING_ORDER + 1, 1);
    gct_wksp->upestimation->timing_coeffs = gct_alloc_matrix(GCT_UPSWING_TIMING_ORDER + 1, 1);
    gct_wksp->downestimation->timing_coeffs = gct_alloc_matrix(GCT_DOWNSWING_TIMING_ORDER + 1, 1);
    gct_wksp->headtrajectory = NULL;
}

void gct_display_2D_preliminary_result
    (
    Mat& im,
    const GCT_point2ushort head
    )
{
    if (head.x != 0 || head.y != 0)
        {
        //note: shown image leads detection result one frame
        circle(im, Point(head.x, head.y), 3, CV_RGB(0, 255, 0), -1, 8, 0);
        }
    imshow("candidates", im);
    waitKey(10);
}

void gct_display_2D_final_result
    (
    Mat& im,
    const int FRAME_IDX,
    const int TRANS_IDX,
    const GCT_point2ushort* headtrajectory
    )
{
    int idx = 0;
    for (idx = 0; idx < FRAME_IDX; ++idx)
        {
        if (idx < TRANS_IDX)
            {
            circle(im, Point(headtrajectory[idx].x, headtrajectory[idx].y), 5, CV_RGB(255, 50 + idx, 50 + idx), -1, 8, 0);
            }
        else if (idx > TRANS_IDX + 10 && (headtrajectory[idx].x != 0 || headtrajectory[idx].y != 0))
            {
            circle(im, Point(headtrajectory[idx].x, headtrajectory[idx].y), 5, CV_RGB(50 + idx - TRANS_IDX, 50 + idx - TRANS_IDX, 255), -1, 8, 0);
            if (headtrajectory[idx - 1].x != 0 || headtrajectory[idx - 1].y != 0)
                {
                line(im, Point(headtrajectory[idx].x, headtrajectory[idx].y), Point(headtrajectory[idx - 1].x, headtrajectory[idx - 1].y),
                     CV_RGB(50 + idx - TRANS_IDX, 50 + idx - TRANS_IDX, 255), 3, 8, 0);
                }
            }
        }
    imshow("result", im);
    waitKey(10);
}

void GCT_main_2D
    (
    const char* videofile,
    const int start_index
    )
{
    // A. init capture
    Mat im;
    VideoCapture cap;
    cap.open(videofile);
    if (!cap.isOpened())
        {
        printf("Failed opening video\n");
        exit(-1);
        }
    cap.set(CV_CAP_PROP_POS_FRAMES, (double)start_index);
    //B. produce hypotheses
    bool is_end = false;
    GCT_point2ushort head_hypothesis = { 0, 0 };
    while (is_end == false)
        {
        //B.1 get frame
        cap >> im;
        printf("%d\n", ++gct_wksp->swingstate->frame_index);
        //B.2 preprocess and detect, pass first two frames
        gct_bgr2gray(im.data, gct_wksp->buffer_gray_next);
        gct_trajectory_preprocess_and_detection(gct_wksp);
        //B.3 update memory, for image subtraction
        unsigned char* tempptr = gct_wksp->buffer_gray_previous;
        gct_wksp->buffer_gray_previous = gct_wksp->buffer_gray_current;
        gct_wksp->buffer_gray_current = gct_wksp->buffer_gray_next;
        gct_wksp->buffer_gray_next = tempptr;
        //B.4 reset stack pointer
        gct_reset_stack_ptr_to_unreserved_position();
        //B.5 write to result
        if (gct_wksp->swingstate->is_found_clubhead)
            {
            head_hypothesis = gct_wksp->swingstate->clubhead_pos_carte[gct_wksp->swingstate->current_index - 1];
            }
        else
            {
            head_hypothesis.x = 0;
            head_hypothesis.y = 0;
            }
        if (gct_wksp->swingstate->is_endnow)
            {
            break;
            }
        //B.6 display result
        gct_display_2D_preliminary_result(im, head_hypothesis);
        }
    destroyAllWindows();
    cap.open(videofile);
    cap.set(CV_CAP_PROP_POS_FRAMES, (double)start_index);
    //C. trajectory fitting
    gct_fitting(gct_wksp);
    //D. display 2D final result
    int frameidx = 0;
    const int NUM_FRAME = gct_wksp->swingstate->frame_index;
    const int TRANS_IDX = gct_wksp->swingstate->clubhead_timing[gct_wksp->swingstate->transition_index];
    const GCT_point2ushort* head_final_traj = gct_wksp->headtrajectory;
    for (frameidx = 0; frameidx < NUM_FRAME; ++frameidx)
        {
        cap >> im;
        gct_display_2D_final_result(im, frameidx, TRANS_IDX, head_final_traj);
        }
    destroyAllWindows();
}

int main
    (
    int ac,
    char **av
    )
{
    if (ac != 3)
        {
        usage_help();
        return EXIT_FAILURE;
        }
    
    GCT_init_2D();
    GCT_main_2D(av[1], atoi(av[2]));
    GCT_release();
    
    return EXIT_SUCCESS;
}

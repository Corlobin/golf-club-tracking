//
//  gct.c
//  opencv_playground
//
//  Created by Lincoln Hard on 2017/2/2.
//  Copyright © 2017年 Lincoln Hard. All rights reserved.
//

#include "gct.h"
#include <math.h>
#include <float.h>

#define IM_SUB_TH (15)
// two below macro should range between 0-1 and are decided empirically by Canny
#define NON_STRONG_EDGE_NUM_RATIO (0.4)
#define STRONG_WEAK_EDGE_TH_RATIO (0.33333)
#define GCT_DEG2RAD (0.017453293)
#define GCT_RAD2DEG (57.2957795f)
#define CLUB_HOUGH_ACCUM_COUNT (40)
#define GCT_INLIER_RADIUS_AVG_DIFF (300)
#define GCT_PINV_SMALL_NUMBER (1.0e-16)
#define GCT_TRIANGULATION_EPSILON (0.0001)
//blob detection
#define MIN_CLUBHEAD_AREA 75
#define MAX_CLUBHEAD_AREA 700
#define GCT_MIN_WH_RATIO 0.4f
#define GCT_MAX_WH_RATIO 2.5f
#define GCT_VALID_DISTANCE 60
//check incremental hypothesis
#define GCT_INLIER_THETA_DIFF (25)
#define GCT_INLIER_RADIUS_DIFF (70)
//check hypthesis domain
#define DOWNSWING_PASS_THETA 40 //skip 0 - 30 degree
#define DOWNSWING_STARTING_INDEX 40 //the beginning of down swing

static const short morph_ellipse_op[21] = { -GCT_IMG_WIDTH - 2, -2, GCT_IMG_WIDTH - 2, -(GCT_IMG_WIDTH << 1) - 1, -GCT_IMG_WIDTH - 1, -1, GCT_IMG_WIDTH - 1, (GCT_IMG_WIDTH << 1) - 1,
    -(GCT_IMG_WIDTH << 1), -GCT_IMG_WIDTH, 0, GCT_IMG_WIDTH, (GCT_IMG_WIDTH << 1), -(GCT_IMG_WIDTH << 1) + 1, -GCT_IMG_WIDTH + 1, 1, GCT_IMG_WIDTH + 1, (GCT_IMG_WIDTH << 1) + 1, -GCT_IMG_WIDTH + 2, 2, GCT_IMG_WIDTH + 2 };
// sigma = 0.75
static const float gaussian_kernel[5] = { 0.0152f, 0.218752f, 0.532097f, 0.218752f, 0.0152f };

static const short connected_neighbor[8] = { -1, -GCT_IMG_WIDTH - 1, -GCT_IMG_WIDTH, -GCT_IMG_WIDTH + 1, 1, 1 + GCT_IMG_WIDTH, GCT_IMG_WIDTH, GCT_IMG_WIDTH - 1 };

static void carte_to_polar
    (
    const GCT_point2ushort* polar_center,
    const GCT_swing_state* swing_state,
    const int pixel_coord_x,
    const int pixel_coord_y,
    float* theta,
    float* radius
    )
{
    float newy = (float)pixel_coord_x - (float)polar_center->x;
    float newx = -((float)pixel_coord_y - (float)polar_center->y);
    *radius = (float)sqrt(newx * newx + newy * newy);
    float temptheta = GCT_RAD2DEG * (float)atan2(newy, newx);
    if (swing_state->is_downswing)
        {
        //downswing range 360 to 0
        *theta = temptheta + 360.0f * (temptheta < 0.0f);
        }
    else
        {
        //upswing range 180 to 360
        *theta = temptheta + 360.0f;
        }
}

static GCT_linked_list_node* alloc_node
    (
    int i,
    int j
    )
{
    GCT_linked_list_node* pt = (GCT_linked_list_node*)gct_alloc_from_stack(sizeof(GCT_linked_list_node));
    pt->next = NULL;
    pt->position.x = i;
    pt->position.y = j;
    return pt;
};

static GCT_2uchar check_surround
    (
    const unsigned char* labels_at_current_position,
    GCT_linked_list_node** obj_list
    )
{
    int i = 0;
    int j = 0;
    unsigned char detected_label[2] = { 0, 0 };
    for (i = 0; i < 4; ++i)
        {
        unsigned char neighbor_label = *(labels_at_current_position + connected_neighbor[i]);
        if (neighbor_label > 0 && obj_list[neighbor_label] != NULL)
            {
            if (neighbor_label != detected_label[0])
                {
                detected_label[j++] = neighbor_label;
                if (j == 2)
                    {
                    break;
                    }
                }
            }
        }
    GCT_2uchar ordered_labels = { 0, 0 };
    if (detected_label[0] > detected_label[1])
        {
        ordered_labels.a = detected_label[0];
        ordered_labels.b = detected_label[1];
        }
    else
        {
        ordered_labels.a = detected_label[1];
        ordered_labels.b = detected_label[0];
        }
    return ordered_labels;
}

static void blob_check
    (
    const GCT_point2ushort* polar_center,
    GCT_swing_state* swing_state,
    const unsigned char* src,
    GCT_point2ushort* clubhead,
    float* theta,
    float* radius
    )
{
    unsigned int stacksize = gct_get_stack_current_alloc_size();
    unsigned char* labels = (unsigned char*)gct_alloc_from_stack(GCT_IMG_WIDTH * GCT_IMG_HEIGHT * sizeof(unsigned char));
    //assume(limit) max number of labels would be under 1 byte
    GCT_linked_list_node** objects_list = (GCT_linked_list_node**)gct_alloc_from_stack(UCHAR_MAX * sizeof(GCT_linked_list_node*));
    GCT_linked_list_node** list_temp_ptr = (GCT_linked_list_node**)gct_alloc_from_stack(UCHAR_MAX * sizeof(GCT_linked_list_node*));    
    int i = 0;
    int j = 0;
    int current_position = 0;
    unsigned char temp_label_count = 0;
    for (j = 1; j < GCT_IMG_HEIGHT - 1; ++j)
        {
        for (i = 1; i < GCT_IMG_WIDTH - 1; ++i)
            {
            current_position = i + j * GCT_IMG_WIDTH;
            //build blob
            if (*(src + current_position) == 0xFF) // replace '== 0xFF' to '!= 0' for expanding but rather noisy result
                {
                //8-connected
                GCT_2uchar surround_labels = check_surround(labels + current_position, objects_list);
                if (surround_labels.a == 0)
                    {
                    //new label
                    ++temp_label_count;
                    labels[current_position] = temp_label_count;
                    //1-index
                    list_temp_ptr[temp_label_count] = objects_list[temp_label_count] = alloc_node(i, j);
                    }
                else
                    {
                    unsigned char min_label = 0;
                    if (surround_labels.b == 0)
                        {
                        //only one label surround
                        min_label = surround_labels.a;
                        }
                    else
                        {
                        // two or three kinds of label surround
                        min_label = surround_labels.b;
                        //merge
                        if (objects_list[surround_labels.a] != NULL)
                            {
                            list_temp_ptr[min_label]->next = objects_list[surround_labels.a];
                            list_temp_ptr[min_label] = list_temp_ptr[surround_labels.a];
                            objects_list[surround_labels.a] = NULL;
                            }
                        }
                    labels[current_position] = min_label;
                    //1-inex
                    list_temp_ptr[min_label]->next = alloc_node(i, j);
                    list_temp_ptr[min_label] = list_temp_ptr[min_label]->next;
                    }
                }
            }
        }
    //1-index
    GCT_blob_info* blob_analyze = (GCT_blob_info*)gct_alloc_from_stack((temp_label_count + 1) * sizeof(GCT_blob_info));
    for (i = 1; i <= temp_label_count; ++i)
        {
        if (objects_list[i] != NULL)
            {
            list_temp_ptr[i] = objects_list[i];
            unsigned short minx = USHRT_MAX;
            unsigned short maxx = 0;
            unsigned short miny = USHRT_MAX;
            unsigned short maxy = 0;
            do
                {
                //0-index
                unsigned short px = list_temp_ptr[i]->position.x;
                unsigned short py = list_temp_ptr[i]->position.y;
                ++blob_analyze[i].area;
                blob_analyze[i].avgx += px;
                blob_analyze[i].avgy += py;
                if (px < minx)
                    {
                    minx = px;
                    }
                if (px > maxx)
                    {
                    maxx = px;
                    }
                if (py < miny)
                    {
                    miny = py;
                    }
                if (py > maxy)
                    {
                    maxy = py;
                    }               
                list_temp_ptr[i] = list_temp_ptr[i]->next;
                } while (list_temp_ptr[i] != NULL);
            
            blob_analyze[i].width = maxx - minx + 1;
            blob_analyze[i].height = maxy - miny + 1;
            blob_analyze[i].avgx = (unsigned int)(((float)blob_analyze[i].avgx / blob_analyze[i].area) + 0.5f);
            blob_analyze[i].avgy = (unsigned int)(((float)blob_analyze[i].avgy / blob_analyze[i].area) + 0.5f);
            
            //debug
            printf("i: %d, area: %d, w: %d, h: %d, x: %d, y: %d\n",
                   i, blob_analyze[i].area, blob_analyze[i].width, blob_analyze[i].height,
                   blob_analyze[i].avgx, blob_analyze[i].avgy);
            
            if (blob_analyze[i].area > MIN_CLUBHEAD_AREA && blob_analyze[i].area < MAX_CLUBHEAD_AREA)
                {
                float woverh = (float)blob_analyze[i].width / blob_analyze[i].height;
                if (woverh >= GCT_MIN_WH_RATIO && woverh <= GCT_MAX_WH_RATIO)
                    {
                    int disx = blob_analyze[i].avgx - swing_state->clubhead_pos_carte[swing_state->current_index - 1].x;
                    int disy = blob_analyze[i].avgy - swing_state->clubhead_pos_carte[swing_state->current_index - 1].y;
                    int dis = (int)(sqrt((double)(disx * disx + disy * disy)) + 0.5);
                    if (dis < GCT_VALID_DISTANCE)
                        {
                        //to carte
                        clubhead->x = blob_analyze[i].avgx;
                        clubhead->y = blob_analyze[i].avgy;
                        //to polar
                        carte_to_polar(polar_center, swing_state, blob_analyze[i].avgx, blob_analyze[i].avgy, theta, radius);
                        swing_state->is_found_clubhead = true;
                        break;
                        }
                    }
                }
            }
        }
    gct_reset_stack_ptr_to_assigned_position(stacksize);
}

static void check_incremental_hypothesis
    (
    GCT_swing_state* swing_state,
    const float theta,
    const float radius
    )
{
    unsigned short refr = 270;
    unsigned short reft = 180;
    if (swing_state->current_index > 0)
        {
        refr = swing_state->clubhead_pos_polar[swing_state->current_index - 1].r;
        reft = swing_state->clubhead_pos_polar[swing_state->current_index - 1].t;
        }
    //save continuous trajectory only
    int delta_radius = (unsigned short)(radius + 0.5f) - refr;
    int delta_theta = (unsigned short)(theta + 0.5f) - reft;
    //printf("delta_radius: %d, delta_theta: %d\n", delta_radius, delta_theta);
    if (CV_ABS(delta_radius) > GCT_INLIER_RADIUS_DIFF || CV_ABS(delta_theta) > GCT_INLIER_THETA_DIFF)
        {
        swing_state->is_found_clubhead = false;
        return;
        }
    if (swing_state->is_downswing)
        {
        //outlier, length too long or too short
        if (CV_ABS(swing_state->stick_length - radius) > GCT_INLIER_RADIUS_AVG_DIFF)
            {
            swing_state->is_found_clubhead = false;
            return;
            }
        //decremental for down swing
        if (delta_theta > 0)
            {
            swing_state->is_found_clubhead = false;
            return;
            }
        }
    else
        {
        //incremental for up swing
        if (delta_theta < 0)
            {
            swing_state->is_found_clubhead = false;
            return;
            }
        }
    swing_state->is_found_clubhead = true;
}

static void check_hypothesis_domain
    (
    GCT_swing_state* swing_state,
    float theta,
    float radius
    )
{
    //downswing range 360 to 0
    if (swing_state->is_downswing == true)
        {
        GCT_FRAME_INDEX downswing_delta_index = swing_state->current_index - swing_state->transition_index;
        if (downswing_delta_index < DOWNSWING_STARTING_INDEX && theta < 90.0f)
            {
            swing_state->is_found_clubhead = false;
            }
        else if (downswing_delta_index > DOWNSWING_STARTING_INDEX && theta < DOWNSWING_PASS_THETA)
            {
            //not start of downswing which can imply its the end of downswing
            swing_state->is_endnow = true;
            swing_state->is_found_clubhead = false;
            }
        }
    else
        {
        //upswing range 180 to 360
        if (theta > 360.0f) //only appears when it's upswing
            {
            swing_state->is_found_clubhead = false;
            return;
            }
        swing_state->stick_length += (unsigned int)(radius + 0.5f);
        }
}

static void updown_transition_detection
    (
    GCT_swing_state* swing_state
    )
{
    if (swing_state->is_downswing == false && swing_state->transition_count > 4 &&
        360 - swing_state->clubhead_pos_polar[swing_state->current_index - 1].t < 45)
        {
        swing_state->is_downswing = true;
        swing_state->transition_index = swing_state->current_index;
        swing_state->stick_length = (unsigned int)(((float)swing_state->stick_length / (float)swing_state->current_index) + 0.5f);
        printf("======transition now=====\n");
        }
}

static void hough_line
    (
    const unsigned char* src,
    int* fit_radius,
    unsigned char* fit_theta
    )
{
    // sqrt(2)/2 = 1/sqrt(2) = 0.707
    int rmax = (int)(0.707f * CV_MAX(GCT_IMG_WIDTH, GCT_IMG_HEIGHT));
    //map col unit: theta, map row unit: radius
    int accu_map_size = 180 * (rmax << 1); //positive and negative radius
    unsigned short *accu = (unsigned short*)gct_alloc_from_stack(accu_map_size * sizeof(unsigned short));
    int center_x = GCT_IMG_WIDTH >> 1;
    int center_y = GCT_IMG_HEIGHT >> 1;
    int i = 0;
    int j = 0;
    int run = 0;
    //range: 0 - 180
    unsigned char theta = 0;
    double radius = 0.0;
    for (j = 0; j < GCT_IMG_HEIGHT; ++j)
        {
        for (i = 0; i < GCT_IMG_WIDTH; ++i)
            {
            if (src[run] == 0xFF)
                {
                for (theta = 0; theta < 180; ++theta)
                    {
                    radius = (i - center_x) * cos(theta * GCT_DEG2RAD) + (j - center_y) * sin(theta * GCT_DEG2RAD);
                    ++accu[theta + 180 * (int)(radius + rmax + 0.5)];
                    }
                }
            ++run;
            }
        }
    int max_idx = 0;
    int max_val = 0;
    for (i = 0; i < accu_map_size; ++i)
        {
        if (accu[i] > max_val)
            {
            max_val = accu[i];
            max_idx = i;
            }
        }
    *fit_theta = max_idx % 180;
    *fit_radius = max_idx / 180;
    gct_partial_free_from_stack(accu_map_size * sizeof(unsigned short));
}

static void hypothesis_extraction
    (
    const unsigned char* srcedge,
    const unsigned char* srcsmoothed,
    GCT_swing_state* swing_state
    )
{
    const GCT_point2ushort polar_center = {GCT_IMG_WIDTH >> 1, GCT_IMG_HEIGHT >> 1};
    int radius = 0;
    unsigned char theta = 0;
    hough_line(srcedge, &radius, &theta);
    int center_x = GCT_IMG_WIDTH >> 1;
    int center_y = GCT_IMG_HEIGHT >> 1;
    //sqrt(2)/2 = 1/sqrt(2) = 0.707
    int rmax = (int)(0.707f * CV_MAX(GCT_IMG_WIDTH, GCT_IMG_HEIGHT));
    //segmentation
    int pixelcount = 0;
    int i = 0;
    int j = 0;
    //GCT_point2ushort segment_end[2] = { { 0, 0 }, { 0, 0 } };
    unsigned short* segment = (unsigned short*)gct_alloc_from_stack(4 * rmax * sizeof(unsigned short));
    if (theta >= 45 && theta <= 135)
        {
        // y = (r - x cos(t)) / sin(t)
        for (i = 0; i < GCT_IMG_WIDTH; ++i)
            {
            j = (int)(((radius - rmax) - ((i - center_x) * cos(theta * GCT_DEG2RAD))) / sin(theta * GCT_DEG2RAD) + 0.5) + center_y;
            if (j >= 0 && j < GCT_IMG_HEIGHT)
                {
                int temppos = GCT_IMG_WIDTH * j + i;
                if (*(srcedge + temppos) == 0xFF || *(srcedge + temppos + 1) == 0xFF || *(srcedge + temppos - 1) == 0xFF)
                    {
                    segment[pixelcount] = i;
                    segment[pixelcount + 1] = j;
                    pixelcount += 2;
                    }
                }
            }
        }
    else
        {
        // x = (r - y sin(t)) / cos(t);
        for (j = 0; j < GCT_IMG_HEIGHT; ++j)
            {
            i = (int)(((radius - rmax) - ((j - center_y) * sin(theta * GCT_DEG2RAD))) / cos(theta * GCT_DEG2RAD) + 0.5) + center_x;
            if (i >= 0 && i < GCT_IMG_WIDTH)
                {
                int temppos = GCT_IMG_WIDTH * j + i;
                if (*(srcedge + temppos) == 0xFF || *(srcedge + temppos + GCT_IMG_WIDTH) == 0xFF || *(srcedge + temppos - GCT_IMG_WIDTH) == 0xFF)
                    {
                    segment[pixelcount] = i;
                    segment[pixelcount + 1] = j;
                    pixelcount += 2;
                    }
                }
            }
        }
    pixelcount = pixelcount >> 1;
    //debug
    printf("hough pixel manual count: %d\n", pixelcount);
    //trustable club
    GCT_point2ushort clubhead_carte = {0, 0};
    //transfer to polar system
    float clubhead_theta = 0.0f;
    float clubhead_radius = 0.0f;
    if (pixelcount < CLUB_HOUGH_ACCUM_COUNT)
        {
        //failed in Hough detection
        swing_state->is_found_clubhead = false;
        if (swing_state->current_index > 0)
            {
            updown_transition_detection(swing_state);
            }
        }
    else
        {
        //the hypothsis should away from center of image(TODO: may change to center of human)
        if ((CV_ABS(segment[0] - polar_center.x) + CV_ABS(segment[1] - polar_center.y)) >
            (CV_ABS(segment[(pixelcount << 1) - 2] - polar_center.x) + CV_ABS(segment[(pixelcount << 1) - 1] - polar_center.y)))
            {
            clubhead_carte.x = segment[0];
            clubhead_carte.y = segment[1];
            }
        else
            {
            clubhead_carte.x = segment[(pixelcount << 1) - 2];
            clubhead_carte.y = segment[(pixelcount << 1) - 1];
            }
        carte_to_polar(&polar_center, swing_state, clubhead_carte.x, clubhead_carte.y, &clubhead_theta, &clubhead_radius);
        
        check_incremental_hypothesis(swing_state, clubhead_theta, clubhead_radius);
        }  
    //blob detection after Hough failure
    if (swing_state->is_found_clubhead == false && swing_state->current_index > 0)
        {
        blob_check(&polar_center, swing_state, srcsmoothed, &clubhead_carte, &clubhead_theta, &clubhead_radius);
        }    
    //finally, check whether clubhead position is in domain or not
    if (swing_state->is_found_clubhead == true)
        {
        check_hypothesis_domain(swing_state, clubhead_theta, clubhead_radius);
        }   
    //debug
    //printf("club x: %d, y: %d, r: %f, t: %f\n", clubhead_carte.x, clubhead_carte.y, clubhead_radius, clubhead_theta);
    //save
    if (swing_state->is_found_clubhead == true)
        {
        swing_state->clubhead_pos_carte[swing_state->current_index].x = clubhead_carte.x;
        swing_state->clubhead_pos_carte[swing_state->current_index].y = clubhead_carte.y;
        swing_state->clubhead_pos_polar[swing_state->current_index].r = (unsigned short)(clubhead_radius + 0.5f);
        swing_state->clubhead_pos_polar[swing_state->current_index].t = (unsigned short)(clubhead_theta + 0.5f);
        swing_state->clubhead_timing[swing_state->current_index] = swing_state->frame_index;
        ++(swing_state->current_index);
        swing_state->transition_count = 0;
        printf("saved!\n");
        }
    else
        {
        ++(swing_state->transition_count);
        }
}

static void connected_neighbor_analysis
    (
    const unsigned short* current_mag_pt,
    unsigned char* current_edge_pt,
    int low_th
    )
{
    // todo: recursive function may leads stack overflow in embedded system
    int i = 0;
    unsigned char* temp_edge_pt = NULL;
    const unsigned short* temp_mag_pt = NULL;
    for (i = 0; i < 8; ++i)
        {
        temp_edge_pt = current_edge_pt + connected_neighbor[i];
        temp_mag_pt = current_mag_pt + connected_neighbor[i];
        if ((*temp_edge_pt == 0x80) && (*temp_mag_pt > low_th))
            {
            *temp_edge_pt = 0xFF;
            connected_neighbor_analysis(temp_mag_pt, temp_edge_pt, low_th);
            }
        }
}

static void double_th_hysteresis
    (
    const unsigned short* grad_magnitude,
    unsigned short* hist,
    unsigned char* edge
    )
{
    // build histogram of gradient magnitude
    int i = 0;
    int max_mag = 0;
    int num_edges = 0;
    const int GCT_IMG_SIZE = GCT_IMG_WIDTH * GCT_IMG_HEIGHT;
    memset(hist, 0, GCT_IMG_SIZE * sizeof(unsigned short));
    for (i = 0; i < GCT_IMG_SIZE; ++i)
        {
        hist[grad_magnitude[i]] += (edge[i] == 0x80);
        num_edges += (edge[i] == 0x80);
        if (grad_magnitude[i] > max_mag)
            {
            max_mag = grad_magnitude[i];
            }
        }
    int non_strong_edge_count = (int)(num_edges * NON_STRONG_EDGE_NUM_RATIO + 0.5);
    for (i = 1, num_edges = 0; num_edges < non_strong_edge_count; ++i)
        {
        num_edges += hist[i];
        }
    int high_th = i;
    int low_th = (int)(high_th * STRONG_WEAK_EDGE_TH_RATIO + 0.5);
    for (i = 0; i < GCT_IMG_SIZE; ++i)
        {
        if (edge[i] == 0x80 && grad_magnitude[i] >= high_th)
            {
            edge[i] = 0xFF;
            connected_neighbor_analysis(grad_magnitude + i, edge + i, low_th);
            }
        }
    for (i = 0; i < GCT_IMG_SIZE; ++i)
        {
        if (edge[i] == 0x80)
            {
            edge[i] = 0;
            }
        }
}

static void non_maximum_suppression
    (
    const short* gradx,
    const short* grady,
    const unsigned short* grad_magnitude,
    unsigned char* result
    )
{
    int i = 0;
    int j = 0;
    int run = GCT_IMG_WIDTH;
    int dx = 0;
    int dy = 0;
    int absgradx = 0;
    int absgrady = 0;
    // side1
    int a1 = 0;
    int a2 = 0;
    int A = 0;
    // side2
    int b1 = 0;
    int b2 = 0;
    int B = 0;
    // middle (current pixel)
    int middle = 0;
    // ignore border
    for (j = 1; j < (GCT_IMG_HEIGHT - 1); ++j)
        {
        for (i = 1; i < (GCT_IMG_WIDTH - 1); ++i)
            {
            ++run;
            if (grad_magnitude[run] == 0)
                {
                continue;
                }
            absgradx = CV_ABS(gradx[run]);
            absgrady = CV_ABS(grady[run]);
            dx = (gradx[run] > 0) ? 1 : -1;
            dy = (grady[run] > 0) ? 1 : -1;
            // interpolate
            if (absgradx > absgrady)
                {
                a1 = grad_magnitude[run + dx];
                a2 = grad_magnitude[run + dx + dy * GCT_IMG_WIDTH];
                b1 = grad_magnitude[run - dx];
                b2 = grad_magnitude[run - dx - dy * GCT_IMG_WIDTH];
                A = (absgradx - absgrady) * a1 + absgrady * a2;
                B = (absgradx - absgrady) * b1 + absgrady * b2;
                middle = grad_magnitude[run] * absgradx;
                }
            else
                {
                a1 = grad_magnitude[run + dy * GCT_IMG_WIDTH];
                a2 = grad_magnitude[run + dx + dy * GCT_IMG_WIDTH];
                b1 = grad_magnitude[run - dy * GCT_IMG_WIDTH];
                b2 = grad_magnitude[run - dx - dy * GCT_IMG_WIDTH];
                A = (absgrady - absgradx) * a1 + absgradx * a2;
                B = (absgrady - absgradx) * b1 + absgradx * b2;
                middle = grad_magnitude[run] * absgrady;
                }
            // suppress
            if (middle > A && middle > B)
                {
                result[run] = 0x80;
                }
            else
                {
                result[run] = 0;
                }
            }
        run += 2;
        }
}

static void calc_gradient
    (
    const unsigned char* src,
    short* gradx,
    short* grady,
    unsigned short* grad_magnitude
    )
{
    int i = 0;
    int j = 0;
    int run = 0;
    const int GCT_IMG_SIZE = GCT_IMG_WIDTH * GCT_IMG_HEIGHT;
    // dx = [-1 0 +1]
    for (j = 0, run = 0; j < GCT_IMG_HEIGHT; ++j)
        {
        // replicate border
        gradx[run] = src[run + 1] - src[run];
        ++run;
        for (i = 1; i < (GCT_IMG_WIDTH - 1); ++i)
            {
            gradx[run] = src[run + 1] - src[run - 1];
            ++run;
            }
        // replicate border
        gradx[run] = src[run] - src[run - 1];
        ++run;
        }
    // dy = [-1 0 +1]'
    for (i = 0; i < GCT_IMG_WIDTH; ++i)
        {
        // replicate border
        run = i;
        grady[run] = src[run + GCT_IMG_WIDTH] - src[run];
        run += GCT_IMG_WIDTH;
        for (j = 1; j < (GCT_IMG_HEIGHT - 1); ++j)
            {
            grady[run] = src[run + GCT_IMG_WIDTH] - src[run - GCT_IMG_WIDTH];
            run += GCT_IMG_WIDTH;
            }
        // replicate border
        grady[run] = src[run] - src[run - GCT_IMG_WIDTH];
        }
    // magnitude, range: 0 - 361
    for (i = 0; i < GCT_IMG_SIZE; ++i)
        {
        grad_magnitude[i] = (unsigned short)(sqrt((double)(gradx[i] * gradx[i] + grady[i] * grady[i])) + 0.5);
        }
}

static void gaussian_smooth
    (
    unsigned char* in_out_im,
    unsigned char* tempbuf
    )
{
    const unsigned int GCT_IMG_SIZE = GCT_IMG_WIDTH * GCT_IMG_HEIGHT;
    // x direction (in_out_im -> tempbuf)
    int i = 0;
    float dotsum = 0.0f;
    int begin = 2;
    int end = GCT_IMG_SIZE - 2;
    // wrap border
    for (i = begin; i < end; ++i)
        {
        dotsum = in_out_im[i - 2] * gaussian_kernel[0] +
        in_out_im[i - 1] * gaussian_kernel[1] +
        in_out_im[i] * gaussian_kernel[2] +
        in_out_im[i + 1] * gaussian_kernel[3] +
        in_out_im[i + 2] * gaussian_kernel[4];
        tempbuf[i] = (unsigned char)(dotsum + 0.5f);
        }
    tempbuf[0] = tempbuf[1] = tempbuf[2];
    tempbuf[GCT_IMG_SIZE - 1] = tempbuf[GCT_IMG_SIZE - 2] = tempbuf[GCT_IMG_SIZE - 3];
    memset(in_out_im, 0, GCT_IMG_SIZE * sizeof(unsigned char));
    // y direction (tempbuf -> in_out_im)
    // main part
    begin = GCT_IMG_WIDTH << 1;
    end = GCT_IMG_SIZE - (GCT_IMG_WIDTH << 1);
    for (i = begin; i < end; ++i)
        {
        dotsum = tempbuf[i - (GCT_IMG_WIDTH << 1)] * gaussian_kernel[0] +
        tempbuf[i - GCT_IMG_WIDTH] * gaussian_kernel[1] +
        tempbuf[i] * gaussian_kernel[2] +
        tempbuf[i + GCT_IMG_WIDTH] * gaussian_kernel[3] +
        tempbuf[i + (GCT_IMG_WIDTH << 1)] * gaussian_kernel[4];
        in_out_im[i] = (unsigned char)(dotsum + 0.5f);
        }
    // side part, zero border
    // first row
    begin = 0;
    end = GCT_IMG_WIDTH;
    for (i = begin; i < end; ++i)
        {
        dotsum = tempbuf[i] * gaussian_kernel[2] +
        tempbuf[i + GCT_IMG_WIDTH] * gaussian_kernel[3] +
        tempbuf[i + (GCT_IMG_WIDTH << 1)] * gaussian_kernel[4];
        in_out_im[i] = (unsigned char)(dotsum / (gaussian_kernel[2] + gaussian_kernel[3] + gaussian_kernel[4]) + 0.5f);
        }  
    // second row
    begin = GCT_IMG_WIDTH;
    end = GCT_IMG_WIDTH << 1;
    for (i = begin; i < end; ++i)
        {
        dotsum = tempbuf[i - GCT_IMG_WIDTH] * gaussian_kernel[1] +
        tempbuf[i] * gaussian_kernel[2] +
        tempbuf[i + GCT_IMG_WIDTH] * gaussian_kernel[3] +
        tempbuf[i + (GCT_IMG_WIDTH << 1)] * gaussian_kernel[4];
        in_out_im[i] = (unsigned char)(dotsum / (gaussian_kernel[1] + gaussian_kernel[2] + gaussian_kernel[3] + gaussian_kernel[4]) + 0.5f);
        }
    // row before last row
    begin = GCT_IMG_SIZE - (GCT_IMG_WIDTH << 1);
    end = GCT_IMG_SIZE - GCT_IMG_WIDTH;
    for (i = begin; i < end; ++i)
        {
        dotsum = tempbuf[i - (GCT_IMG_WIDTH << 1)] * gaussian_kernel[0] +
        tempbuf[i - GCT_IMG_WIDTH] * gaussian_kernel[1] +
        tempbuf[i] * gaussian_kernel[2] +
        tempbuf[i + GCT_IMG_WIDTH] * gaussian_kernel[3];
        in_out_im[i] = (unsigned char)(dotsum / (gaussian_kernel[0] + gaussian_kernel[1] + gaussian_kernel[2] + gaussian_kernel[3]) + 0.5f);
        }
    // last row
    begin = GCT_IMG_SIZE - GCT_IMG_WIDTH;
    end = GCT_IMG_SIZE;
    for (i = begin; i < end; ++i)
        {
        dotsum = tempbuf[i - (GCT_IMG_WIDTH << 1)] * gaussian_kernel[0] +
        tempbuf[i - GCT_IMG_WIDTH] * gaussian_kernel[1] +
        tempbuf[i] * gaussian_kernel[2];
        in_out_im[i] = (unsigned char)(dotsum / (gaussian_kernel[0] + gaussian_kernel[1] + gaussian_kernel[2]) + 0.5f);
        }
}

static void canny_edge
    (
    unsigned char* src,
    unsigned char* dst
    )
{
    const unsigned int GCT_IMG_SIZE = GCT_IMG_WIDTH * GCT_IMG_HEIGHT;
    gaussian_smooth(src, dst);
    short* dx = (short*)gct_alloc_from_stack(GCT_IMG_SIZE * sizeof(short));
    short* dy = (short*)gct_alloc_from_stack(GCT_IMG_SIZE * sizeof(short));
    unsigned short* magnitude_xy = (unsigned short*)gct_alloc_from_stack(GCT_IMG_SIZE * sizeof(unsigned short));
    calc_gradient(src, dx, dy, magnitude_xy);
    memset(dst, 0, GCT_IMG_SIZE * sizeof(unsigned char));
    non_maximum_suppression(dx, dy, magnitude_xy, dst);
    double_th_hysteresis(magnitude_xy, (unsigned short*)dx, dst);
    gct_partial_free_from_stack(3 * GCT_IMG_SIZE * sizeof(short));
}

static void morph_erode
    (
    const unsigned char* src,
    unsigned char* dst
    )
{
    int i = 0;
    int j = 0;
    int k = 0;
    unsigned char flag = 1;
    const int GCT_IMG_SIZE = GCT_IMG_WIDTH * GCT_IMG_HEIGHT;
    for (i = 0; i < GCT_IMG_SIZE; ++i, j = 0, flag = 1)
        {
        while (flag && j < 21)
            {
            k = i + morph_ellipse_op[j++];
            flag *= (k >= 0 && k < GCT_IMG_SIZE && src[k] != 0);
            }
        dst[i] = 0xFF * flag;
        }
}

static void morph_dilate
    (
    const unsigned char* src,
    unsigned char* dst
    )
{
    int i = 0;
    int j = 0;
    int k = 0;
    const int GCT_IMG_SIZE = GCT_IMG_WIDTH * GCT_IMG_HEIGHT;
    for (i = 0; i < GCT_IMG_SIZE; ++i, j = 0)
        {
        while (src[i] && j < 21)
            {
            k = i + morph_ellipse_op[j++];
            k *= (k >= 0 && k < GCT_IMG_SIZE);
            dst[k] = 1;
            }
        }
}

static void diff_abs_thres_and
    (
    const GCT_work_space_type* gct_wksp,
    unsigned char* dst
    )
{
    const int GCT_IMG_SIZE = GCT_IMG_WIDTH * GCT_IMG_HEIGHT;
    int i = 0;
    for (i = 0; i < GCT_IMG_SIZE; ++i)
        {
        dst[i] = (CV_ABS(gct_wksp->buffer_gray_current[i] - gct_wksp->buffer_gray_previous[i]) > IM_SUB_TH) * (CV_ABS(gct_wksp->buffer_gray_next[i] - gct_wksp->buffer_gray_current[i]) > IM_SUB_TH);
        }
}

void gct_trajectory_preprocess_and_detection
    (
    const GCT_work_space_type* gct_wksp
    )
{
    const unsigned int GCT_IMG_SIZE = GCT_IMG_WIDTH * GCT_IMG_HEIGHT;
    unsigned char* temp_buffer1 = (unsigned char*)gct_alloc_from_stack(GCT_IMG_SIZE * sizeof(unsigned char));
    unsigned char* temp_buffer2 = (unsigned char*)gct_alloc_from_stack(GCT_IMG_SIZE * sizeof(unsigned char));
    diff_abs_thres_and(gct_wksp, temp_buffer1);
    morph_dilate(temp_buffer1, temp_buffer2);
    memset(temp_buffer1, 0, GCT_IMG_SIZE * sizeof(unsigned char));
    morph_erode(temp_buffer2, temp_buffer1);
    memset(temp_buffer2, 0, GCT_IMG_SIZE * sizeof(unsigned char));
    canny_edge(temp_buffer1, temp_buffer2);
    hypothesis_extraction(temp_buffer2, temp_buffer1, gct_wksp->swingstate);
}

void gct_bgr2hsv
    (
    const unsigned char* bgr_src,
    unsigned char* hsv_dst
    )
{
    const int GCT_IMG_SIZE = GCT_IMG_WIDTH * GCT_IMG_HEIGHT;
    int i = 0;
    int j = 0;
    float rchannel_pixelvalue = 0.0f;
    float gchannel_pixelvalue = 0.0f;
    float bchannel_pixelvalue = 0.0f;
    float bgrmax = 0.0f;
    float bgrmin = 0.0f;
    float hnormalized = 0.0f;
    float snormalized = 0.0f;
    float vnormalized = 0.0f;
    for (i = 0, j = 0; i < GCT_IMG_SIZE; ++i, j += 3)
        {
        bchannel_pixelvalue = bgr_src[j] / 255.0f;
        gchannel_pixelvalue = bgr_src[j + 1] / 255.0f;
        rchannel_pixelvalue = bgr_src[j + 2] / 255.0f;
        bgrmax = three_var_max_fl(bchannel_pixelvalue, gchannel_pixelvalue, rchannel_pixelvalue);
        bgrmin = three_var_min_fl(bchannel_pixelvalue, gchannel_pixelvalue, rchannel_pixelvalue);
        vnormalized = bgrmax;
        if (bgrmax == 0.0f || bgrmax == bgrmin)
            {
            hnormalized = 0.0f;
            snormalized = 0.0f;
            }
        else
            {
            snormalized = (bgrmax - bgrmin) / bgrmax;
            if (bgrmax == rchannel_pixelvalue)
                {
                hnormalized = 60.0f * (gchannel_pixelvalue - bchannel_pixelvalue) / (bgrmax - bgrmin);
                }
            else if (bgrmax == gchannel_pixelvalue)
                {
                hnormalized = 120.0f + 60.0f * (bchannel_pixelvalue - rchannel_pixelvalue) / (bgrmax - bgrmin);
                }
            else
                {
                hnormalized = 240.0f + 60.0f * (rchannel_pixelvalue - gchannel_pixelvalue) / (bgrmax - bgrmin);
                }
            }
        if (hnormalized < 0.0f)
            {
            hnormalized += 360.0f;
            }
        hsv_dst[j] = (unsigned char)(hnormalized * 0.5f + 0.5f);
        hsv_dst[j + 1] = (unsigned char)(snormalized * 255.0f);
        hsv_dst[j + 2] = (unsigned char)(vnormalized * 255.0f);
        }
}

void gct_bgr2gray
    (
    const unsigned char* bgr_src,
    unsigned char* gray_dst
    )
{
    const int GCT_IMG_SIZE = GCT_IMG_WIDTH * GCT_IMG_HEIGHT;
    int i = 0;
    int j = 0;
    for (i = 0; i < GCT_IMG_SIZE; ++i, j += 3)
        {
        gray_dst[i] = (unsigned char)(0.299f * bgr_src[j + 2] + 0.587f * bgr_src[j + 1] + 0.114f * bgr_src[j] + 0.5f);
        }
}

void gct_image_transpose
    (
    unsigned char* im,
    int channel_num
    )
{
    const unsigned int fullsize = channel_num * GCT_IMG_WIDTH * GCT_IMG_HEIGHT;
    const unsigned int fullheight = channel_num * GCT_IMG_HEIGHT;
    unsigned char* buf = (unsigned char*)gct_alloc_from_stack(fullsize * sizeof(unsigned char));
    unsigned char* bufrunptr = buf;
    unsigned char* imrunptr = im;
    const unsigned char* im_end = im + fullsize - 1;
    const unsigned char* buf_end = buf + fullsize;
    while (bufrunptr != buf_end)
        {
        memcpy(bufrunptr, imrunptr, channel_num * sizeof(unsigned char));
        bufrunptr += channel_num;
        //note the GCT_IMG_HEIGHT here is the destination image height which is also the image "width" of source image
        imrunptr += fullheight;
        if (imrunptr > im_end)
            {
            imrunptr -= (fullsize - channel_num);
            }
        }
    memcpy(im, buf, fullsize * sizeof(unsigned char));
    gct_partial_free_from_stack(fullsize * sizeof(unsigned char));
}

void gct_channel_one_image_flip
    (
    unsigned char* im
    )
{
    int i = 0;
    int j = 0;
    int k = 0;
    unsigned char* rowptr = im;
    for (k = 0; k < GCT_IMG_HEIGHT; ++k)
        {
        for (i = 0, j = GCT_IMG_WIDTH - 1; i < j; ++i, --j)
            {
            rowptr[i] ^= rowptr[j];
            rowptr[j] ^= rowptr[i];
            rowptr[i] ^= rowptr[j];
            }
        rowptr += GCT_IMG_WIDTH;
        }
}

void gct_image_flip
    (
    unsigned char* im,
    int channel_num
    )
{
    int i = 0;
    int j = 0;
    int k = 0;
    const unsigned int fullwidth = channel_num * GCT_IMG_WIDTH;
    unsigned char* rowptr = im;
    unsigned char temp[3] = { 0 };
    for (k = 0; k < GCT_IMG_HEIGHT; ++k)
        {
        for (i = 0, j = fullwidth - channel_num; i < j; i += channel_num, j -= channel_num)
            {
            memcpy(temp, rowptr + i, channel_num * sizeof(unsigned char));
            memcpy(rowptr + i, rowptr + j, channel_num * sizeof(unsigned char));
            memcpy(rowptr + j, temp, channel_num * sizeof(unsigned char));
            }
        rowptr += fullwidth;
        }
}

void gct_image_rotate_clockwise
    (
    unsigned char* im,
    int channel_num
    )
{
    const unsigned int fullsize = channel_num * GCT_IMG_WIDTH * GCT_IMG_HEIGHT;
    const unsigned int fullheight = channel_num * GCT_IMG_HEIGHT;
    unsigned char* buf = (unsigned char*)gct_alloc_from_stack(fullsize * sizeof(unsigned char));
    unsigned char* bufrunptr = buf;
    unsigned char* imrunptr = im + fullsize - fullheight;
    const unsigned char* buf_end = buf + fullsize;
    while (bufrunptr != buf_end)
        {
        memcpy(bufrunptr, imrunptr, channel_num * sizeof(unsigned char));
        bufrunptr += channel_num;
        //note the GCT_IMG_HEIGHT here is the destination image height which is also the image "width" of source image
        imrunptr -= fullheight;
        if (imrunptr < im)
            {
            imrunptr += (fullsize + channel_num);
            }
        }
    memcpy(im, buf, fullsize * sizeof(unsigned char));
    gct_partial_free_from_stack(fullsize * sizeof(unsigned char));
}

static double pythag
    (
    double a,
    double b
    )
{
    double absa = fabs(a);
    double absb = fabs(b);
    if (absa > absb)
        {
        return absa * sqrt(1.0 + CV_POW2(absb / absa));
        }
    else
        {
        return (absb == 0.0 ? 0.0 : absb * sqrt(1.0 + CV_POW2(absa / absb)));
        }
}

static void svdcmp
    (
    double** a,
    unsigned int m,
    unsigned int n,
    double w[],
    double** v
    )
{
    bool flag = 0;
    unsigned int i = 1;
    int its = 1;
    unsigned int j = 1;
    unsigned int jj = 1;
    unsigned int k = 1;
    unsigned int l = 1;
    unsigned int nm = 1;
    double anorm = 0.0;
    double scale = 0.0;
    double g = 0.0;
    double c = 0.0;
    double f = 0.0;
    double h = 0.0;
    double s = 0.0;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    
    double* rv1 = gct_alloc_vector(n);
    for (i = 1; i <= n; ++i)
        {
        l = i + 1;
        rv1[i] = scale*g;
        g = s = scale = 0.0;
        if (i <= m)
            {
            for (k = i; k <= m; ++k)
                {
                scale += fabs(a[k][i]);
                }
            if (scale)
                {
                for (k = i; k <= m; ++k)
                    {
                    a[k][i] /= scale;
                    s += a[k][i] * a[k][i];
                    }
                f = a[i][i];
                g = -GCT_SVDSIGN(sqrt(s), f);
                h = f*g - s;
                a[i][i] = f - g;
                for (j = l; j <= n; ++j)
                    {
                    for (s = 0.0, k = i; k <= m; ++k)
                        {
                        s += a[k][i] * a[k][j];
                        }
                    f = s / h;
                    for (k = i; k <= m; ++k)
                        {
                        a[k][j] += f*a[k][i];
                        }
                    }
                for (k = i; k <= m; ++k)
                    {
                    a[k][i] *= scale;
                    }
                }
            }
        w[i] = scale *g;
        g = s = scale = 0.0;
        if (i <= m && i != n)
            {
            for (k = l; k <= n; ++k)
                {
                scale += fabs(a[i][k]);
                }
            if (scale)
                {
                for (k = l; k <= n; ++k)
                    {
                    a[i][k] /= scale;
                    s += a[i][k] * a[i][k];
                    }
                f = a[i][l];
                g = -GCT_SVDSIGN(sqrt(s), f);
                h = f*g - s;
                a[i][l] = f - g;
                for (k = l; k <= n; ++k)
                    {
                    rv1[k] = a[i][k] / h;
                    }
                for (j = l; j <= m; ++j)
                    {
                    for (s = 0.0, k = l; k <= n; ++k)
                        {
                        s += a[j][k] * a[i][k];
                        }
                    for (k = l; k <= n; ++k)
                        {
                        a[j][k] += s*rv1[k];
                        }
                    }
                for (k = l; k <= n; ++k)
                    {
                    a[i][k] *= scale;
                    }
                }
            }
        anorm = CV_MAX(anorm, (fabs(w[i]) + fabs(rv1[i])));
        }
    for (i = n; i >= 1; --i)
        {
        if (i < n)
            {
            if (g)
                {
                for (j = l; j <= n; ++j)
                    {
                    v[j][i] = (a[i][j] / a[i][l]) / g;
                    }
                for (j = l; j <= n; ++j)
                    {
                    for (s = 0.0, k = l; k <= n; ++k)
                        {
                        s += a[i][k] * v[k][j];
                        }
                    for (k = l; k <= n; ++k)
                        {
                        v[k][j] += s*v[k][i];
                        }
                    }
                }
            for (j = l; j <= n; ++j)
                {
                v[i][j] = v[j][i] = 0.0;
                }
            }
        v[i][i] = 1.0;
        g = rv1[i];
        l = i;
        }
    for (i = CV_MIN(m, n); i >= 1; --i)
        {
        l = i + 1;
        g = w[i];
        for (j = l; j <= n; ++j)
            {
            a[i][j] = 0.0;
            }
        if (g)
            {
            g = 1.0 / g;
            for (j = l; j <= n; ++j)
                {
                for (s = 0.0, k = l; k <= m; ++k)
                    {
                    s += a[k][i] * a[k][j];
                    }
                f = (s / a[i][i]) * g;
                for (k = i; k <= m; ++k)
                    {
                    a[k][j] += f*a[k][i];
                    }
                }
            for (j = i; j <= m; ++j)
                {
                a[j][i] *= g;
                }
            }
        else
            {
            for (j = i; j <= m; ++j)
                {
                a[j][i] = 0.0;
                }
            }
        ++a[i][i];
        }
    for (k = n; k >= 1; --k)
        {
        for (its = 1; its <= 30; ++its)
            {
            flag = 1;
            for (l = k; l >= 1; --l)
                {
                nm = l - 1;
                if ((double)(fabs(rv1[l]) + anorm) == anorm)
                    {
                    flag = 0;
                    break;
                    }
                if ((double)(fabs(w[nm]) + anorm) == anorm)
                    {
                    break;
                    }
                }
            if (flag)
                {
                c = 0.0;
                s = 1.0;
                for (i = l; i <= k; ++i)
                    {
                    f = s*rv1[i];
                    rv1[i] = c*rv1[i];
                    if ((double)(fabs(f) + anorm) == anorm)
                        {
                        break;
                        }
                    g = w[i];
                    h = pythag(f, g);
                    w[i] = h;
                    h = 1.0 / h;
                    c = g*h;
                    s = -f*h;
                    for (j = 1; j <= m; ++j)
                        {
                        y = a[j][nm];
                        z = a[j][i];
                        a[j][nm] = y*c + z*s;
                        a[j][i] = z*c - y*s;
                        }
                    }
                }
            z = w[k];
            if (l == k)
                {
                if (z < 0.0)
                    {
                    w[k] = -z;
                    for (j = 1; j <= n; ++j)
                        {
                        v[j][k] = -v[j][k];
                        }
                    }
                break;
                }
            x = w[l];
            nm = k - 1;
            y = w[nm];
            g = rv1[nm];
            h = rv1[k];
            f = ((y - z) * (y + z) + (g - h) * (g + h)) / (2.0 * h * y);
            g = pythag(f, 1.0);
            f = ((x - z) * (x + z) + h * ((y / (f + GCT_SVDSIGN(g, f))) - h)) / x;
            c = s = 1.0;
            for (j = l; j <= nm; ++j)
                {
                i = j + 1;
                g = rv1[i];
                y = w[i];
                h = s*g;
                g = c*g;                
                z = pythag(f, h);
                rv1[j] = z;
                c = f / z;
                s = h / z;
                f = x*c + g*s;
                g = g*c - x*s;
                h = y * s;
                y *= c;
                for (jj = 1; jj <= n; ++jj)
                    {
                    x = v[jj][j];
                    z = v[jj][i];
                    v[jj][j] = x*c + z*s;
                    v[jj][i] = z*c - x*s;
                    }
                z = pythag(f, h);
                w[j] = z;
                if (z)
                    {
                    z = 1.0 / z;
                    c = f*z;
                    s = h*z;
                    }
                f = c*g + s*y;
                x = c*y - s*g;
                for (jj = 1; jj <= m; ++jj)
                    {
                    y = a[jj][j];
                    z = a[jj][i];
                    a[jj][j] = y*c + z*s;
                    a[jj][i] = z*c - y*s;
                    }
                }
            rv1[l] = 0.0;
            rv1[k] = f;
            w[k] = x;
            }
        }
    gct_free_vector(n);
}

static void transpose_matrix
    (
    double** src,
    double** dst,
    unsigned int src_rows,
    unsigned int src_cols
    )
{
    unsigned int i = 1;
    unsigned int j = 1;
    for (j = 1; j <= src_rows; ++j)
        {
        for (i = 1; i <= src_cols; ++i)
            {
            dst[i][j] = src[j][i];
            }
        }
}

static void multiply_matrix
    (
    double** a,
    double** b,
    double** c,
    unsigned int m,
    unsigned int n,
    unsigned int l
    )
{
    //c = a*b,   a is m by n, b is n by l, hence c is m by l
    unsigned int i = 1;
    unsigned int j = 1;
    unsigned int k = 1;
    for (i = 1; i <= m; ++i)
        {
        for (j = 1; j <= l; ++j)
            {
            c[i][j] = 0.0;
            for (k = 1; k <= n; ++k)
                {
                c[i][j] += a[i][k] * b[k][j];
                }
            }
        }
}

static double multiply_vector
    (
    double* a,
    double* b,
    unsigned int len
    )
{
    //a is 1 by len, b is len by 1
    unsigned int i = 1;
    double sum = 0.0;
    for (i = 1; i <= len; ++i)
        {
        sum += a[i] * b[i];
        }
    return sum;
}

static void svd_to_inverse
    (
    double** U,
    double* w,
    double** V,
    double** inv,
    int M,
    int N
    )
{
    //U is M by N; w is N ; V is N by N; inv is N by M dimensional
    int i = 1;
    int j = 1;
    int k = 1;
    double** tmp_NN = gct_alloc_matrix(N, N);
    double** tmp_w = gct_alloc_matrix(N, N);  
    /*tmp_NN = V * 1/w */
    for (i = 1; i <= N; ++i)
        {
        for (j = 1; j <= N; ++j)
            {
            tmp_w[i][j] = 0.0;
            }
        }
    for (i = 1; i <= N; ++i)
        {
        if (w[i] != 0.0)
            {
            tmp_w[i][i] = 1 / w[i];
            }
        else
            {
            tmp_w[i][i] = 0.0;
            }
        } 
    multiply_matrix(V, tmp_w, tmp_NN, N, N, N);  
    /*inv = tmp_NN * U' */
    for (i = 1; i <= N; ++i)
        {
        for (j = 1; j <= M; ++j)
            {
            inv[i][j] = 0.0;
            for (k = 1; k <= N; ++k)
                {
                inv[i][j] += tmp_NN[i][k] * U[j][k];
                }
            }
        }
    gct_free_matrix(N, N);
    gct_free_matrix(N, N);
}

static void pinv
    (
    double* const* A,
    double** invA,
    unsigned int M,
    unsigned int N
    )
{
    //1-index numbering
    //invA is inverse of A, A is M by N, invA is N by M
    double wmin = 0.0;
    double wmax = 0.0;
    unsigned int i = 1;
    double** U = gct_alloc_matrix(M, N);
    double** V = gct_alloc_matrix(M, N);
    double* w = gct_alloc_vector(N);   
    //copy A --> U, svdcmp() will modify input matrix
    memcpy(U[1], A[1], (M * N + 1) * sizeof(double));
    svdcmp(U, M, N, w, V);
    for (i = 1; i <= N; ++i)
        {
        if (w[i] > wmax)
            {
            wmax = w[i];
            }
        }
    wmin = wmax * GCT_PINV_SMALL_NUMBER;
    for (i = 1; i <= N; ++i)
        {
        if (w[i] < wmin)
            {
            w[i] = 0.0;
            }
        }
    svd_to_inverse(U, w, V, invA, M, N);
    gct_free_vector(N);
    gct_free_matrix(M, N);
    gct_free_matrix(M, N);
}

static void solve_linear
    (
    double** A,
    double** B,
    double** dst,
    unsigned int m,
    unsigned int n
    )
{
    //A: input matrix on the left-hand side of the system
    //B: input matrix on the right-hand side of the system
    //dst: output solution
    //m: rows of A
    //n: cols of A
    double** A_prime = gct_alloc_matrix(n, m); //A'
    double** A_gram = gct_alloc_matrix(n, n); //A'A
    double** A_gram_inv = gct_alloc_matrix(n, n); //inv(A'A)
    double** A_gram_inv_A_prime = gct_alloc_matrix(n, m); //inv(A'A)A'
    transpose_matrix(A, A_prime, m, n);
    multiply_matrix(A_prime, A, A_gram, n, m, n);
    pinv(A_gram, A_gram_inv, n, n);
    multiply_matrix(A_gram_inv, A_prime, A_gram_inv_A_prime, n, n, m);
    multiply_matrix(A_gram_inv_A_prime, B, dst, n, m, 1);
    gct_free_matrix(n, m);
    gct_free_matrix(n, n);
    gct_free_matrix(n, n);
    gct_free_matrix(n, m);
}

static void time_fitting
    (
    const GCT_swing_state* swing_state,
    const GCT_FRAME_INDEX* inlier_index,
    const GCT_FRAME_INDEX inlier_num,
    const int fitting_order,
    GCT_estimation_result* estimation_result
    )
{
    GCT_FRAME_INDEX i = 0;
    GCT_FRAME_INDEX j = 0;
    double* nomalized_timing = (double*)gct_alloc_from_stack(inlier_num * sizeof(double));
    double* nomalized_theta = (double*)gct_alloc_from_stack(inlier_num * sizeof(double));
    //normalize
    for (i = 0; i < inlier_num; ++i)
        {
        nomalized_timing[i] = (double)(swing_state->clubhead_timing[inlier_index[i]]) / GCT_TIME_NORMALIZED_RANGE;
        nomalized_theta[i] = (double)(swing_state->clubhead_pos_polar[inlier_index[i]].t) / GCT_THETA_NORMALIZED_RANGE;
        }
    // in order to find a good approximation, we use all inliers found previously to compute the model (no RANSAC idea here now)
    const int arows = inlier_num;
    const int acols = fitting_order + 1;
    double** A = gct_alloc_matrix(arows, acols);
    double** T = gct_alloc_matrix(arows, 1);
    double** C = gct_alloc_matrix(acols, 1);
    //operation buffer
    double** A_prime = gct_alloc_matrix(acols, arows); //A'
    double** A_gram = gct_alloc_matrix(acols, acols); //A'A
    double** A_gram_inv = gct_alloc_matrix(acols, acols); //inv(A'A)
    double** A_gram_inv_A_prime = gct_alloc_matrix(acols, arows); //inv(A'A)A'
    for (i = 1; i <= arows; ++i)
        {
        A[i][1] = 1.0;
        double timing = nomalized_timing[i - 1];
        double theta = nomalized_theta[i - 1];
        for (j = 1; j < acols; ++j)
            {
            A[i][j + 1] = pow(timing, (double)j);
            }
        T[i][1] = theta;
        }
    transpose_matrix(A, A_prime, arows, acols);
    multiply_matrix(A_prime, A, A_gram, acols, arows, acols);
    pinv(A_gram, A_gram_inv, acols, acols);
    multiply_matrix(A_gram_inv, A_prime, A_gram_inv_A_prime, acols, acols, arows);
    multiply_matrix(A_gram_inv_A_prime, T, C, acols, arows, 1);
    memcpy(estimation_result->timing_coeffs[1], C[1], (acols + 1) * sizeof(double));    
    gct_free_matrix(acols, arows);
    gct_free_matrix(acols, acols);
    gct_free_matrix(acols, acols);
    gct_free_matrix(acols, arows);
    gct_free_matrix(acols, 1);
    gct_free_matrix(arows, 1);
    gct_free_matrix(arows, acols);
    gct_partial_free_from_stack(inlier_num * sizeof(double));
    gct_partial_free_from_stack(inlier_num * sizeof(double));
}

static GCT_FRAME_INDEX trajectory_fitting
    (
    const GCT_swing_state* swing_state,
    const GCT_FRAME_INDEX data_start_index,
    const GCT_FRAME_INDEX data_end_index,
    const int fitting_order,
    const int num_iteration,
    GCT_estimation_result* estimation_result,
    GCT_FRAME_INDEX* onfit_index_optimal
    )
{
    GCT_FRAME_INDEX i = 0;
    GCT_FRAME_INDEX j = 0;
    GCT_FRAME_INDEX k = 0;
    const int GCT_RADIUS_NORMALIZED_RANGE = (int)(0.5 * sqrt(GCT_IMG_WIDTH * GCT_IMG_WIDTH + GCT_IMG_HEIGHT * GCT_IMG_HEIGHT));
    const GCT_FRAME_INDEX data_amount = data_end_index - data_start_index;
    double* nomalized_theta = (double*)gct_alloc_from_stack(data_amount * sizeof(double));
    double* nomalized_radius = (double*)gct_alloc_from_stack(data_amount * sizeof(double));
    //normalize
    for (i = data_start_index, j = 0; i < data_end_index; ++i, ++j)
        {
        nomalized_theta[j] = (double)(swing_state->clubhead_pos_polar[i].t) / GCT_THETA_NORMALIZED_RANGE;
        nomalized_radius[j] = (double)(swing_state->clubhead_pos_polar[i].r) / GCT_RADIUS_NORMALIZED_RANGE;
        }
    /*
     * fitting model: upswing is represented by a order 4 polar polynomial function
     *                downswing is represented by a order 6 polar polynomial function
     * estimation: get C s.t. ||AC - R|| is minimum, A is the theta polynomial term, R is the corresponding radius
     */
    //should larger than num of cols, and depend on inlier/outlier ratio, large arows -> underfitting, small arows -> overfitting
    const int arows = fitting_order + 10;
    //add one constant term
    const int acols = fitting_order + 1;
    double** A = gct_alloc_matrix(arows, acols);
    double** R = gct_alloc_matrix(arows, 1);
    double** C = gct_alloc_matrix(acols, 1);
    //operation buffer
    double** A_prime = gct_alloc_matrix(acols, arows); //A'
    double** A_gram = gct_alloc_matrix(acols, acols); //A'A
    double** A_gram_inv = gct_alloc_matrix(acols, acols); //inv(A'A)
    double** A_gram_inv_A_prime = gct_alloc_matrix(acols, arows); //inv(A'A)A'
    //evaluate buffer
    double** A_eval = gct_alloc_matrix(data_amount, acols);
    double** R_eval = gct_alloc_matrix(data_amount, 1);
    GCT_FRAME_INDEX* onfit_index = (GCT_FRAME_INDEX*)gct_alloc_from_stack(data_amount * sizeof(GCT_FRAME_INDEX));
    double sum_r_diff = 0.0;
    //root mean square error
    double rmse = 0.0;
    double min_rmse = DBL_MAX;
    GCT_FRAME_INDEX support_count = 0;
    GCT_FRAME_INDEX inlier_count = 0;
    //start iteration
    for (i = 0; i < num_iteration; ++i, support_count = 0, sum_r_diff = 0.0)
        {
        //1. randomly pick may-be-inliers
        for (j = 1; j <= arows; ++j)
            {
            int idx = rand() % (data_amount);
            double theta = nomalized_theta[idx];
            double radius = nomalized_radius[idx];
            A[j][1] = 1.0;
            for (k = 1; k < acols; ++k)
                {
                A[j][k + 1] = pow(theta, (double)k);
                }
            R[j][1] = radius;
            }
        //2. may-be-inliers making their may-be-correct-model
        transpose_matrix(A, A_prime, arows, acols);
        multiply_matrix(A_prime, A, A_gram, acols, arows, acols);
        pinv(A_gram, A_gram_inv, acols, acols);
        multiply_matrix(A_gram_inv, A_prime, A_gram_inv_A_prime, acols, acols, arows);
        multiply_matrix(A_gram_inv_A_prime, R, C, acols, arows, 1);
        //debug
        //gct_print_matrix(C, acols, 1);
        //3. save inliers, calulate RMSE
        for (j = 1; j <= data_amount; ++j)
            {
            A_eval[j][1] = 1.0;
            for (k = 1; k < acols; ++k)
                {
                A_eval[j][k + 1] = pow(nomalized_theta[j - 1], (double)k);
                }
            }
        multiply_matrix(A_eval, C, R_eval, data_amount, acols, 1);
        for (j = 1; j <= data_amount; ++j)
            {
            double rdiff = CV_ABS(R_eval[j][1] - nomalized_radius[j - 1]);
            if (rdiff < 2.0e-002)
                {
                //inliers
                onfit_index[support_count] = j - 1 + data_start_index;
                ++support_count;
                }
            sum_r_diff += (rdiff * rdiff);
            }
        rmse = sqrt(sum_r_diff / data_amount);
        //4. save the best fit model
        if (rmse < min_rmse)
            {
            min_rmse = rmse;
            memcpy(estimation_result->fitting_coeffs[1], C[1], (acols + 1) * sizeof(double));
            memcpy(onfit_index_optimal, onfit_index, support_count * sizeof(GCT_FRAME_INDEX));
            inlier_count = support_count;
            //debug
            printf("inlier rate: %f\n", (float)inlier_count / data_amount);
            printf("rmse: %f\n", rmse);
            }
        }//end of iteration
    
    gct_partial_free_from_stack(data_amount * sizeof(GCT_FRAME_INDEX));
    gct_free_matrix(data_amount, 1);
    gct_free_matrix(data_amount, acols);
    gct_free_matrix(acols, arows);
    gct_free_matrix(acols, acols);
    gct_free_matrix(acols, acols);
    gct_free_matrix(acols, arows);
    gct_free_matrix(acols, 1);
    gct_free_matrix(arows, 1);
    gct_free_matrix(arows, acols);
    gct_partial_free_from_stack(data_amount * sizeof(double));
    gct_partial_free_from_stack(data_amount * sizeof(double));
    
    return inlier_count;
}

void trajectory_estimation_and_fitting
    (
    const GCT_work_space_type* gct_wksp
    )
{
    //for RANSAC-LIKE iteration
    srand((unsigned int)time(NULL));
    //upswing
    GCT_FRAME_INDEX sample_start_index = 0;
    GCT_FRAME_INDEX sample_end_index = gct_wksp->swingstate->transition_index;
    //the extreme situation is that all hypothesis are inliers
    GCT_FRAME_INDEX* upswing_inlier_index =
    (GCT_FRAME_INDEX*)gct_alloc_from_stack((sample_end_index - sample_start_index)* sizeof(GCT_FRAME_INDEX));
    //1. fit trajectory model(theta, radius)
    GCT_FRAME_INDEX num_inlier = trajectory_fitting(gct_wksp->swingstate, sample_start_index, sample_end_index,
                                                    GCT_UPSWING_FITTING_ORDER, GCT_UPFITTING_ITERATION_ROUNDS, gct_wksp->upestimation, upswing_inlier_index);
    //2. fit timing model(frame index, theta)
    time_fitting(gct_wksp->swingstate, upswing_inlier_index, num_inlier, GCT_UPSWING_TIMING_ORDER, gct_wksp->upestimation);
    //downswing
    sample_start_index = gct_wksp->swingstate->transition_index;
    sample_end_index = gct_wksp->swingstate->current_index;
    //the extreme situation is that all hypothesis are inliers
    GCT_FRAME_INDEX* downswing_inlier_index =
    (GCT_FRAME_INDEX*)gct_alloc_from_stack((sample_end_index - sample_start_index)* sizeof(GCT_FRAME_INDEX));
    //1. fit trajectory model(theta, radius)
    num_inlier = trajectory_fitting(gct_wksp->swingstate, sample_start_index, sample_end_index,
                                    GCT_DOWNSWING_FITTING_ORDER, GCT_DOWNFITTING_ITERATION_ROUNDS, gct_wksp->downestimation, downswing_inlier_index);
    //2. fit timing model(frame index, theta)
    time_fitting(gct_wksp->swingstate, downswing_inlier_index, num_inlier, GCT_DOWNSWING_TIMING_ORDER, gct_wksp->downestimation);

#if 0 //debug
    printf("UP theta-radius coeffs:\n");
    gct_print_matrix(gct_wksp->upestimation->fitting_coeffs, GCT_UPSWING_FITTING_ORDER + 1, 1);
    printf("DOWN theta-radius trcoeffs:\n");
    gct_print_matrix(gct_wksp->downestimation->fitting_coeffs, GCT_DOWNSWING_FITTING_ORDER + 1, 1);
    
    printf("UP time-theta coeffs:\n");
    gct_print_matrix(gct_wksp->upestimation->timing_coeffs, GCT_UPSWING_TIMING_ORDER + 1, 1);
    printf("DOWN time-theta coeffs:\n");
    gct_print_matrix(gct_wksp->downestimation->timing_coeffs, GCT_DOWNSWING_TIMING_ORDER + 1, 1);
#endif
}

void calc_time_related_trajectory
    (
    const GCT_work_space_type* gct_wksp
    )
{
    const int GCT_RADIUS_NORMALIZED_RANGE = (int)(0.5 * sqrt(GCT_IMG_WIDTH * GCT_IMG_WIDTH + GCT_IMG_HEIGHT * GCT_IMG_HEIGHT));
    //idx here stands for time index which is equivalent to frame index
    GCT_FRAME_INDEX idx = 0;
    const GCT_FRAME_INDEX transition_index = gct_wksp->swingstate->clubhead_timing[gct_wksp->swingstate->transition_index];
    const GCT_FRAME_INDEX num_frame = gct_wksp->swingstate->frame_index;
    GCT_point2ushort* head_carte_coord = gct_wksp->headtrajectory;
    int i = 0;
    int timing_curve_order = GCT_UPSWING_TIMING_ORDER;
    int fitting_curve_order = GCT_UPSWING_FITTING_ORDER;
    GCT_estimation_result* est_ptr = gct_wksp->upestimation;
    double normalized_time = 0.0;
    double normalized_theta = 0.0;
    double normalized_radius = 0.0;
    double theta = 0.0;
    double radius = 0.0;
    double previous_theta = 0.0;
    const GCT_point2ushort polar_center = {GCT_IMG_WIDTH >> 1, GCT_IMG_HEIGHT >> 1};
    while (idx < transition_index)
        {
        //upswing
        normalized_time = (double)(idx) / GCT_TIME_NORMALIZED_RANGE;
        for (i = 0; i <= timing_curve_order; ++i)
            {
            normalized_theta += (pow(normalized_time, (double)i) * est_ptr->timing_coeffs[i + 1][1]);
            }
        for (i = 0; i <= fitting_curve_order; ++i)
            {
            normalized_radius += (pow(normalized_theta, (double)i) * est_ptr->fitting_coeffs[i + 1][1]);
            }
        theta = normalized_theta * GCT_THETA_NORMALIZED_RANGE;
        radius = normalized_radius * GCT_RADIUS_NORMALIZED_RANGE;
        
        //out of valid fitting range
        if (theta < 180.0 || theta > 360.0 || theta < previous_theta)
            {
            head_carte_coord[idx].x = 0;
            head_carte_coord[idx].y = 0;
            }
        else
            {
            head_carte_coord[idx].x = (unsigned short)(radius * sin(theta * GCT_DEG2RAD) + polar_center.x + 0.5);
            head_carte_coord[idx].y = (unsigned short)(polar_center.y - radius * cos(theta * GCT_DEG2RAD) + 0.5);
            }
        previous_theta = theta;
        normalized_theta = 0.0;
        normalized_radius = 0.0;
        ++idx;
        }
    
    timing_curve_order = GCT_DOWNSWING_TIMING_ORDER;
    fitting_curve_order = GCT_DOWNSWING_FITTING_ORDER;
    est_ptr = gct_wksp->downestimation;
    previous_theta = 360.0;
    double avgradius = (double)gct_wksp->swingstate->stick_length;
    double previous_radius = 0.0;
    double delta_radius = 0.0;
    double previous_delta_radius = 0.0;
    unsigned char local_extrema_num = 0;
    double delta_theta = 0.0;
    
    while (idx < num_frame)
        {
        //downswing has more range limitation, and we calc avg/max speed here
        normalized_time = (double)(idx) / GCT_TIME_NORMALIZED_RANGE;
        for (i = 0; i <= timing_curve_order; ++i)
            {
            normalized_theta += (pow(normalized_time, (double)i) * est_ptr->timing_coeffs[i + 1][1]);
            }
        for (i = 0; i <= fitting_curve_order; ++i)
            {
            normalized_radius += (pow(normalized_theta, (double)i) * est_ptr->fitting_coeffs[i + 1][1]);
            }
        theta = normalized_theta * GCT_THETA_NORMALIZED_RANGE;
        radius = normalized_radius * GCT_RADIUS_NORMALIZED_RANGE;
        //time-radius curve should look like a second order curve
        delta_radius = radius - previous_radius;
        if (theta < 180.0 && previous_delta_radius * delta_radius < 0.0)
            {
            ++local_extrema_num;
            if (local_extrema_num > 2)
                {
                memset(head_carte_coord + idx * sizeof(GCT_point2ushort), 0, (num_frame - idx) * sizeof(GCT_point2ushort));
                break;
                }
            }
        //out of valid fitting range
        delta_theta = previous_theta - theta;
        if (theta > 360.0 || theta < 0.0 || delta_theta < 0.0 || CV_ABS(radius - avgradius) > GCT_INLIER_RADIUS_AVG_DIFF)
            {
            head_carte_coord[idx].x = 0;
            head_carte_coord[idx].y = 0;
            }
        else
            {
            head_carte_coord[idx].x = (unsigned short)(radius * sin(theta * GCT_DEG2RAD) + polar_center.x + 0.5);
            head_carte_coord[idx].y = (unsigned short)(polar_center.y - radius * cos(theta * GCT_DEG2RAD) + 0.5);
            }
        previous_theta = theta;
        previous_radius = radius;
        previous_delta_radius = delta_radius;
        normalized_theta = 0.0;
        normalized_radius = 0.0;
        ++idx;
        }
}

void gct_fitting
    (
    GCT_work_space_type* gct_wksp
    )
{
    gct_wksp->headtrajectory =
    (GCT_point2ushort*)gct_alloc_from_stack(gct_wksp->swingstate->frame_index * sizeof(GCT_point2ushort));
    //fitting between 1. angle and radius 2. time and angle
    trajectory_estimation_and_fitting(gct_wksp);
    //using fitting coefficients
    calc_time_related_trajectory(gct_wksp);
}

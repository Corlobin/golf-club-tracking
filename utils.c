//
//  utils.c
//  opencv_playground
//
//  Created by Lincoln Hard on 2017/2/2.
//  Copyright © 2017年 Lincoln Hard. All rights reserved.
//

#include "utils.h"
#include "gct.h"

static GCT_stack gct_stack = { NULL, NULL, 0 };

void gct_init_stack
    (
    void
    )
{
    gct_stack.stack_starting_address = malloc(GCT_STACK_TOTAL_SIZE * sizeof(unsigned char));
    if (gct_stack.stack_starting_address == NULL)
        {
        printf("failed creating memory stack\n");
        exit(-1);
        }
    gct_stack.stack_current_address = (char*)gct_stack.stack_starting_address;
    gct_stack.stack_current_alloc_size = 0;
}

void gct_free_stack
    (
    void
    )
{
    free(gct_stack.stack_starting_address);
}

void gct_reset_stack_ptr_to_initial_position
    (
    void
    )
{
    gct_stack.stack_current_address = (char*)gct_stack.stack_starting_address;
    gct_stack.stack_current_alloc_size = 0;
}

void gct_reset_stack_ptr_to_unreserved_position
    (
    void
    )
{
    gct_stack.stack_current_address = (char*)gct_stack.stack_starting_address + GCT_STACK_RESERVED_SIZE;
    gct_stack.stack_current_alloc_size = GCT_STACK_RESERVED_SIZE;
}

void gct_reset_stack_ptr_to_assigned_position
    (
    unsigned int assigned_size
    )
{
    gct_stack.stack_current_address = (char*)gct_stack.stack_starting_address + assigned_size;
    gct_stack.stack_current_alloc_size = assigned_size;
}

unsigned int gct_get_stack_current_alloc_size
    (
    void
    )
{
    return gct_stack.stack_current_alloc_size;
}

void* gct_alloc_from_stack
    (
    unsigned int len
    )
{
    void* ptr = NULL;
    if (len <= 0)
        {
        len = 0x20;
        }
    unsigned int aligned_len = (len + 0xF) & (~0xF);
    gct_stack.stack_current_alloc_size += aligned_len;
    if (gct_stack.stack_current_alloc_size >= GCT_STACK_TOTAL_SIZE)
        {
        printf("failed allocating memory from stack anymore\n");
        free(gct_stack.stack_starting_address);
        exit(-1);
        }
    ptr = gct_stack.stack_current_address;
    gct_stack.stack_current_address += aligned_len;
    //C99: all zero bits means 0 for fixed points, 0.0 for floating points
    memset(ptr, 0, len);
    return ptr;
}

void gct_partial_free_from_stack
    (
    unsigned int len
    )
{
    unsigned int aligned_len = (len + 0xF) & (~0xF);
    gct_stack.stack_current_alloc_size -= aligned_len;
    gct_stack.stack_current_address -= aligned_len;
}

double* gct_alloc_vector
    (
    unsigned int len
    )
{
    //1-index numbering
    double* v = (double*)gct_alloc_from_stack((len + 1)*sizeof(double));
    return v;
}

void gct_free_vector
    (
    unsigned int len
    )
{
    gct_partial_free_from_stack((len + 1)*sizeof(double));
}

void gct_print_vector
    (
    double* V,
    unsigned int len
    )
{
    unsigned int i = 1;
    for (i = 1; i <= len; ++i)
        {
        printf("%f\n", V[i]);
        }
}

double** gct_alloc_matrix
    (
    unsigned int nrow,
    unsigned int ncol
    )
{
    //1-index numbering
    double** m = (double**)gct_alloc_from_stack((nrow + 1) * sizeof(double*));
    m[1] = (double*)gct_alloc_from_stack((nrow * ncol + 1) * sizeof(double));
    unsigned int i = 0;
    for (i = 2; i <= nrow; ++i)
        {
        m[i] = m[i - 1] + ncol;
        }
    return m;
}

void gct_free_matrix
    (
    unsigned int nrow,
    unsigned int ncol
    )
{
    gct_partial_free_from_stack((nrow * ncol + 1) * sizeof(double));
    gct_partial_free_from_stack((nrow + 1) * sizeof(double*));
}

void gct_print_matrix
    (
    double** M,
    unsigned int nrow,
    unsigned int ncol
    )
{
    unsigned int i = 1;
    unsigned int j = 1;
    for (j = 1; j <= nrow; ++j)
        {
        for (i = 1; i <= ncol; ++i)
            {
            printf("%f ", M[j][i]);
            }
        printf("\n");
        }
}

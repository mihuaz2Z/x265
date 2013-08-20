/*****************************************************************************
 * Copyright (C) 2013 x265 project
 *
 * Authors: Gopu Govindaswamy <gopu@multicorewareinc.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02111, USA.
 *
 * This program is also available under a commercial proprietary license.
 * For more information, contact us at licensing@multicorewareinc.com.
 *****************************************************************************/

#include "TLibCommon/TComPic.h"
#include "lowres.h"
#include "mv.h"

using namespace x265;

void Lowres::create(TComPic *pic, int _bframes)
{
    TComPicYuv *orig = pic->getPicYuvOrg();
    TComPicSym *sym = pic->getPicSym();

    isLowres = true;
    bframes = _bframes;
    width = orig->getWidth() / 2;
    lines = orig->getHeight() / 2;
    lumaStride = width + 2 * orig->getLumaMarginX();
    if (lumaStride & 31)
        lumaStride += 32 - (lumaStride & 31);

    /* allocate lowres buffers */
    for (int i = 0; i < 4; i++)
    {
        buffer[i] = (Pel*)X265_MALLOC(Pel, lumaStride * (lines + 2 * orig->getLumaMarginY()));
    }

    int padoffset = lumaStride * orig->getLumaMarginY() + orig->getLumaMarginX();
    lumaPlane[0][0] = buffer[0] + padoffset;
    lumaPlane[2][0] = buffer[1] + padoffset;
    lumaPlane[0][2] = buffer[2] + padoffset;
    lumaPlane[2][2] = buffer[3] + padoffset;

    /* for now, use HPEL planes for QPEL offsets */
    lumaPlane[0][1] = lumaPlane[1][0] = lumaPlane[1][1] = lumaPlane[0][0];
    lumaPlane[2][1] = lumaPlane[3][0] = lumaPlane[3][1] = lumaPlane[2][0];
    lumaPlane[0][3] = lumaPlane[1][2] = lumaPlane[1][3] = lumaPlane[0][2];
    lumaPlane[2][3] = lumaPlane[3][2] = lumaPlane[3][3] = lumaPlane[2][2];

    intraCost = (int*)X265_MALLOC(int, sym->getNumberOfCUsInFrame());

    for (int i = 0; i < bframes + 2; i++)
    {
        for (int j = 0; j < bframes + 2; j++)
        {   
            rowSatds[i][j] = (int*)X265_MALLOC(int, sym->getFrameHeightInCU());
            lowresCosts[i][j] = (uint16_t*)X265_MALLOC(uint16_t, sym->getNumberOfCUsInFrame());
        }
    }

    for (int i = 0; i < bframes + 1; i++)
    {
        lowresMvs[0][i] = (MV*)X265_MALLOC(MV, sym->getNumberOfCUsInFrame());
        lowresMvs[1][i] = (MV*)X265_MALLOC(MV, sym->getNumberOfCUsInFrame());
        lowresMvCosts[0][i] = (int*)X265_MALLOC(int, sym->getNumberOfCUsInFrame());
        lowresMvCosts[1][i] = (int*)X265_MALLOC(int, sym->getNumberOfCUsInFrame());
    }
}

void Lowres::destroy()
{
    for (int i = 0; i < 4; i++)
    {
        if (buffer[i])
            X265_FREE(buffer[i]);
    }

    if (intraCost) X265_FREE(intraCost);

    for (int i = 0; i < bframes + 2; i++)
    {
        for (int j = 0; j < bframes + 2; j++)
        {   
            if (rowSatds[i][j]) X265_FREE(rowSatds[i][j]);
            if (lowresCosts[i][j]) X265_FREE(lowresCosts[i][j]);
        }
    }

    for (int i = 0; i < bframes + 1; i++)
    {
        if (lowresMvs[0][i]) X265_FREE(lowresMvs[0][i]);
        if (lowresMvs[1][i]) X265_FREE(lowresMvs[1][i]);
        if (lowresMvCosts[0][i]) X265_FREE(lowresMvCosts[0][i]);
        if (lowresMvCosts[1][i]) X265_FREE(lowresMvCosts[1][i]);
    }
}

// (re) initialize lowres state
void Lowres::init(TComPicYuv *orig)
{
    bIntraCalculated = false;
    memset(costEst, -1, sizeof(costEst));
    sliceType = X265_SLICE_TYPE_AUTO;
    for (int y = 0; y < bframes + 2; y++)
    {
        for (int x = 0; x < bframes + 2; x++)
        {
            rowSatds[y][x][0] = -1;
        }
    }
    for (int i = 0; i < bframes + 1; i++)
    {
        lowresMvs[0][i][0].x = 0x7FFF;
        lowresMvs[1][i][0].x = 0x7FFF;
    }

    /* downscale and generate 4 HPEL planes for lookahead */
    x265::primitives.frame_init_lowres_core(orig->getLumaAddr(),
        lumaPlane[0][0], lumaPlane[2][0], lumaPlane[0][2], lumaPlane[2][2],
        orig->getStride(), lumaStride, width, lines);

    /* extend hpel planes for motion search */
    orig->xExtendPicCompBorder(lumaPlane[0][0], lumaStride, width, lines, orig->getLumaMarginX(), orig->getLumaMarginY());
    orig->xExtendPicCompBorder(lumaPlane[2][0], lumaStride, width, lines, orig->getLumaMarginX(), orig->getLumaMarginY());
    orig->xExtendPicCompBorder(lumaPlane[0][2], lumaStride, width, lines, orig->getLumaMarginX(), orig->getLumaMarginY());
    orig->xExtendPicCompBorder(lumaPlane[2][2], lumaStride, width, lines, orig->getLumaMarginX(), orig->getLumaMarginY());
}
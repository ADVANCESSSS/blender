/*
 * ***** BEGIN GPL LICENSE BLOCK *****
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * The Original Code is Copyright (C) 2001-2002 by NaN Holding BV.
 * All rights reserved.
 *
 * The Original Code is: all of this file.
 *
 * Contributor(s): none yet.
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file RAS_IPolygonMaterial.h
 *  \ingroup bgerast
 */

#ifndef __RAS_IPOLYGONMATERIAL_H__
#define __RAS_IPOLYGONMATERIAL_H__

#include "RAS_Texture.h"
#include "RAS_MeshObject.h"
#include "RAS_Rasterizer.h"

#include <string>
#include <map>

struct Material;
struct Scene;
struct GameSettings;

enum MaterialProps
{
	RAS_MULTILIGHT = (1 << 1),
	RAS_BLENDERGLSL = (1 << 3),
	RAS_CASTSHADOW = (1 << 4),
	RAS_ONLYSHADOW = (1 << 5),
};

enum MaterialRasterizerModes
{
	RAS_ZSORT = (1 << 0),
	RAS_ALPHA = (1 << 1),
	RAS_DEPTH_ALPHA = (1 << 2),
	RAS_ALPHA_SHADOW = (1 << 3),
	RAS_WIRE = (1 << 4),
	RAS_TEXT = (1 << 5),
	RAS_TWOSIDED = (1 << 6),
};

/**
 * Polygon Material on which the material buckets are sorted
 */
class RAS_IPolyMaterial
{
protected:
	std::string m_name; // also needed for collisionsensor
	int m_drawingmode;
	int m_alphablend;
	float m_zoffset;
	int m_rasMode;
	unsigned int m_flag;

	RAS_Texture *m_textures[RAS_Texture::MaxUnits];

public:

	// care! these are taken from blender polygonflags, see file DNA_mesh_types.h for #define TF_BILLBOARD etc.
	enum MaterialFlags
	{
		BILLBOARD_SCREENALIGNED = 512, // GEMAT_HALO
		BILLBOARD_AXISALIGNED = 1024, // GEMAT_BILLBOARD
		SHADOW = 2048 // GEMAT_SHADOW
	};

	RAS_IPolyMaterial(const std::string& name,
	                  GameSettings *game);

	virtual ~RAS_IPolyMaterial();

	virtual void Activate(RAS_Rasterizer *rasty) = 0;
	virtual void Desactivate(RAS_Rasterizer *rasty) = 0;
	virtual void ActivateInstancing(RAS_Rasterizer *rasty, void *matrixoffset, void *positionoffset, void *coloroffset, unsigned int stride) = 0;
	virtual void DesactivateInstancing() = 0;
	virtual void ActivateMeshSlot(RAS_MeshSlot *ms, RAS_Rasterizer *rasty, const mt::mat3x4& camtrans) = 0;

	bool IsAlpha() const;
	bool IsAlphaDepth() const;
	bool IsZSort() const;
	bool IsWire() const;
	bool IsText() const;
	bool IsCullFace() const;
	int GetDrawingMode() const;
	int GetAlphaBlend() const;
	float GetZOffset() const;
	virtual std::string GetName();
	unsigned int GetFlag() const;
	bool IsAlphaShadow() const;
	bool CastsShadows() const;
	bool OnlyShadow() const;
	RAS_Texture *GetTexture(unsigned int index);

	virtual const std::string GetTextureName() const = 0;
	virtual Material *GetBlenderMaterial() const = 0;
	virtual Scene *GetBlenderScene() const = 0;
	virtual SCA_IScene *GetScene() const = 0;
	virtual bool UseInstancing() const = 0;
	virtual void ReleaseMaterial() = 0;
	virtual void GetRGBAColor(unsigned char *rgba) const;
	virtual bool UsesLighting() const;

	virtual void UpdateIPO(const mt::vec4 &rgba, const mt::vec3 &specrgb, float hard, float spec, float ref,
						   float emit, float ambient, float alpha, float specalpha) = 0;

	virtual const RAS_Rasterizer::AttribLayerList GetAttribLayers(const RAS_MeshObject::LayersInfo& layersInfo) const = 0;

	/**
	 * \return the equivalent drawing mode for the material settings (equivalent to old TexFace tface->mode).
	 */
	int ConvertFaceMode(struct GameSettings *game) const;

	/*
	 * PreCalculate texture gen
	 */
	virtual void OnConstruction() = 0;
};

#endif  /* __RAS_IPOLYGONMATERIAL_H__ */

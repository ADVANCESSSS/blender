/*
 * Detects if an object has moved
 *
 *
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

/** \file gameengine/Ketsji/KX_MovementSensor.cpp
 *  \ingroup ketsji
 */


#include "KX_MovementSensor.h"
#include "SCA_EventManager.h"
#include "SCA_LogicManager.h"
#include "SCA_IObject.h"
#include "KX_GameObject.h"
#include "DNA_sensor_types.h"

#include <stdio.h>


KX_MovementSensor::KX_MovementSensor(SCA_EventManager *eventmgr,
									 SCA_IObject *gameobj,
									 int axis, bool localflag,
									 float threshold)
	:SCA_ISensor(gameobj, eventmgr),
	m_localflag(localflag),
	m_axis(axis),
	m_threshold(threshold)
{
	Init();
}

void KX_MovementSensor::Init()
{
	m_previousPosition = GetOwnerPosition(m_localflag);
	m_positionHasChanged = false;
	m_triggered = (m_invert) ? true : false;
}

KX_MovementSensor::~KX_MovementSensor()
{
}

mt::vec3 KX_MovementSensor::GetOwnerPosition(bool local)
{
	KX_GameObject *owner = (KX_GameObject *)GetParent();
	if (!local) {
		return owner->NodeGetWorldPosition();
	}
	return owner->NodeGetLocalOrientation().Inverse() * owner->NodeGetLocalPosition();
}

CValue *KX_MovementSensor::GetReplica()
{
	KX_MovementSensor *replica = new KX_MovementSensor(*this);
	replica->ProcessReplica();
	replica->Init();

	return replica;
}

bool KX_MovementSensor::IsPositiveTrigger()
{
	bool result = m_positionHasChanged;

	if (m_invert) {
		result = !result;
	}

	return result;
}

bool KX_MovementSensor::Evaluate()
{
	mt::vec3 currentposition;

	bool result = false;
	bool reset = m_reset && m_level;
	
	currentposition = GetOwnerPosition(m_localflag);

	m_positionHasChanged = false;

	switch (m_axis)
	{
		case SENS_MOVEMENT_X_AXIS: // X
		{
			m_positionHasChanged = ((currentposition.x - m_previousPosition.x) > m_threshold);
			break;
		}
		case SENS_MOVEMENT_Y_AXIS: // Y
		{
			m_positionHasChanged = ((currentposition.y - m_previousPosition.y) > m_threshold);
			break;
		}
		case SENS_MOVEMENT_Z_AXIS: // Z
		{
			m_positionHasChanged = ((currentposition.z - m_previousPosition.z) > m_threshold);
			break;
		}
		case SENS_MOVEMENT_NEG_X_AXIS: // -X
		{
			m_positionHasChanged = ((currentposition.x - m_previousPosition.x) < -m_threshold);
			break;
		}
		case SENS_MOVEMENT_NEG_Y_AXIS: // -Y
		{
			m_positionHasChanged = ((currentposition.y - m_previousPosition.y) < -m_threshold);
			break;
		}
		case SENS_MOVEMENT_NEG_Z_AXIS: // -Z
		{
			m_positionHasChanged = ((currentposition.z - m_previousPosition.z) < -m_threshold);
			break;
		}
		case SENS_MOVEMENT_ALL_AXIS: // ALL
		{
			if ((fabs(currentposition.x - m_previousPosition.x) > m_threshold) ||
				(fabs(currentposition.y - m_previousPosition.y) > m_threshold) ||
				(fabs(currentposition.z - m_previousPosition.z) > m_threshold))
			{
				m_positionHasChanged = true;
			}
			break;
		}
	}

	m_previousPosition = currentposition;

	/* now pass this result*/

	if (m_positionHasChanged) {
		if (!m_triggered) {
			// notify logicsystem that movement sensor is just activated
			result = true;
			m_triggered = true;
		}
		else {
			// notify logicsystem that movement sensor is STILL active ...
			result = false;
		}
	}
	else {
		if (m_triggered) {
			m_triggered = false;
			// notify logicsystem that movement is just deactivated
			result = true;
		}
		else {
			result = false;
		}

	}
	if (reset) {
		// force an event
		result = true;
	}

	return result;
}

#ifdef WITH_PYTHON

/* ------------------------------------------------------------------------- */
/* Python functions                                                          */
/* ------------------------------------------------------------------------- */

/* Integration hooks ------------------------------------------------------- */
PyTypeObject KX_MovementSensor::Type = {
	PyVarObject_HEAD_INIT(nullptr, 0)
	"KX_MovementSensor",
	sizeof(PyObjectPlus_Proxy),
	0,
	py_base_dealloc,
	0,
	0,
	0,
	0,
	py_base_repr,
	0,0,0,0,0,0,0,0,0,
	Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
	0,0,0,0,0,0,0,
	Methods,
	0,
	0,
	&SCA_ISensor::Type,
	0,0,0,0,0,0,
	py_base_new
};

PyMethodDef KX_MovementSensor::Methods[] = {
	{nullptr, nullptr} // Sentinel
};

PyAttributeDef KX_MovementSensor::Attributes[] = {
	KX_PYATTRIBUTE_FLOAT_RW("threshold", 0.001f, 10000.0f, KX_MovementSensor, m_threshold),
	KX_PYATTRIBUTE_INT_RW("axis", 0, 6, true, KX_MovementSensor, m_axis),
	KX_PYATTRIBUTE_NULL // Sentinel
};

#endif


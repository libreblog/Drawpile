/*
   DrawPile - a collaborative drawing program.

   Copyright (C) 2008-2013 Calle Laakkonen

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.

*/

#include <QtEndian>

#include "annotation.h"

namespace protocol {

AnnotationCreate *AnnotationCreate::deserialize(const uchar *data, uint len)
{
	if(len!=9)
		return 0;
	return new AnnotationCreate(
		*data,
		qFromBigEndian<quint16>(data+1),
		qFromBigEndian<quint16>(data+3),
		qFromBigEndian<quint16>(data+5),
		qFromBigEndian<quint16>(data+7)
	);
}

int AnnotationCreate::payloadLength() const
{
	return 1 + 4*2;
}

int AnnotationCreate::serializePayload(uchar *data) const
{
	*data = _id; ++data;
	qToBigEndian(_x, data); data += 2;
	qToBigEndian(_y, data); data += 2;
	qToBigEndian(_w, data); data += 2;
	qToBigEndian(_h, data); data += 2;
	return 1 + 4*2;
}

int AnnotationReshape::payloadLength() const
{
	return 1 + 4*2;
}

AnnotationReshape *AnnotationReshape::deserialize(const uchar *data, uint len)
{
	if(len!=9)
		return 0;
	return new AnnotationReshape(
		*data,
		qFromBigEndian<quint16>(data+1),
		qFromBigEndian<quint16>(data+3),
		qFromBigEndian<quint16>(data+5),
		qFromBigEndian<quint16>(data+7)
	);
}

int AnnotationReshape::serializePayload(uchar *data) const
{
	*data = _id; ++data;
	qToBigEndian(_x, data); data += 2;
	qToBigEndian(_y, data); data += 2;
	qToBigEndian(_w, data); data += 2;
	qToBigEndian(_h, data); data += 2;
	return 1 + 4*2;
}

AnnotationEdit *AnnotationEdit::deserialize(const uchar *data, uint len)
{
	return new AnnotationEdit(
		*data,
		qFromBigEndian<quint32>(data+1),
		QByteArray((const char*)data+5, len-5)
	);
}

int AnnotationEdit::payloadLength() const
{
	return 1 + 4 + _text.length();
}

int AnnotationEdit::serializePayload(uchar *data) const
{
	*data = _id; ++data;
	qToBigEndian(_bg, data); data += 4;
	memcpy(data, _text.constData(), _text.length());
	return 1 + 4 + _text.length();
}

AnnotationDelete *AnnotationDelete::deserialize(const uchar *data, uint len)
{
	if(len != 1)
		return 0;
	return new AnnotationDelete(*data);
}

int AnnotationDelete::payloadLength() const
{
	return 1;
}

int AnnotationDelete::serializePayload(uchar *data) const
{
	*data = _id; ++data;
	return 1;
}

}

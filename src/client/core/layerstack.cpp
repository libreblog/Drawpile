/*
   Drawpile - a collaborative drawing program.

   Copyright (C) 2008-2018 Calle Laakkonen

   Drawpile is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   Drawpile is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with Drawpile.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <QDebug>
#include <QPainter>
#include <QMimeData>
#include <QDataStream>

#include "layer.h"
#include "layerstack.h"
#include "tile.h"
#include "rasterop.h"
#include "concurrent.h"

namespace paintcore {

LayerStack::LayerStack(QObject *parent)
	: QObject(parent), m_width(0), m_height(0), m_viewmode(NORMAL), m_viewlayeridx(0),
	  m_onionskinsBelow(4), m_onionskinsAbove(4), m_onionskinTint(true), m_viewBackgroundLayer(true)
{
	m_annotations = new AnnotationModel(this);
}

LayerStack::LayerStack(const LayerStack *orig, QObject *parent)
	: QObject(parent),
	  m_width(orig->m_width),
	  m_height(orig->m_height),
	  m_xtiles(orig->m_xtiles),
	  m_ytiles(orig->m_ytiles),
	  m_viewmode(orig->m_viewmode),
	  m_viewlayeridx(orig->m_viewlayeridx),
	  m_onionskinsBelow(orig->m_onionskinsBelow),
	  m_onionskinTint(orig->m_onionskinTint),
	  m_viewBackgroundLayer(orig->m_viewBackgroundLayer)
{
	m_annotations = orig->m_annotations->clone(this);
	for(const Layer *l : orig->m_layers)
		m_layers << new Layer(*l, this);
}

LayerStack::~LayerStack()
{
	for(Layer *l : m_layers)
		delete l;
}

void LayerStack::reset()
{
	const QSize oldsize(m_width, m_height);
	m_width = 0;
	m_height = 0;
	m_xtiles = 0;
	m_ytiles = 0;
	for(Layer *l : m_layers)
		delete l;
	m_layers.clear();
	m_annotations->clear();
	emit resized(0, 0, oldsize);
	emit layersChanged(QList<LayerInfo>());
}

void LayerStack::removePreviews()
{
	for(Layer *l : m_layers) {
		l->removePreviews();
	}
}

void LayerStack::resize(int top, int right, int bottom, int left)
{
	const QSize oldsize(m_width, m_height);

	int newtop = -top;
	int newleft = -left;
	int newright = m_width + right;
	int newbottom = m_height + bottom;
	if(newtop >= newbottom || newleft >= newright) {
		qWarning() << "Invalid resize: borders reversed";
		return;
	}
	m_width = newright - newleft;
	m_height = newbottom - newtop;

	m_xtiles = Tile::roundTiles(m_width);
	m_ytiles = Tile::roundTiles(m_height);
	m_dirtytiles = QBitArray(m_xtiles*m_ytiles, true);

	for(Layer *l : m_layers)
		l->resize(top, right, bottom, left);

	if(left || top) {
		// Update annotation positions
		QPoint offset(left, top);
		for(const Annotation &a : m_annotations->getAnnotations()) {
			m_annotations->reshapeAnnotation(a.id, a.rect.translated(offset));
		}
	}

	emit resized(left, top, oldsize);
}

/**
 * @param id ID of the new layer
 * @param source source layer ID (used when copy or insert is true)
 * @param color background color (used when copy is false)
 * @param insert if true, the new layer is inserted above source (source 0 inserts at the bottom of the stack)
 * @param copy if true, the layer content is copied from the source
 * @param name layer title
 * @return newly created layer or null in case of error
 */
Layer *LayerStack::createLayer(int id, int source, const QColor &color, bool insert, bool copy, const QString &name)
{
	if(getLayer(id)) {
		qWarning("Layer #%d already exists!", id);
		return nullptr;
	}

	if(m_width<=0 || m_height<=0) {
		// We tolerate this, but in normal operation the canvas size should be
		// set before creating any layers.
		qWarning("Layer created before canvas size was set!");
	}

	// Find source layer if specified
	int sourceIdx=-1;
	if(source>0) {
		for(int i=0;i<m_layers.size();++i) {
			if(m_layers.at(i)->id() == source) {
				sourceIdx = i;
				break;
			}
		}

		if(sourceIdx<0) {
			qWarning("Source layer %d not found!", source);
			return nullptr;
		}
	}

	// Create or copy new layer
	Layer *nl;
	if(copy) {
		if(sourceIdx<0) {
			qWarning("No layer copy source specified!");
			return nullptr;
		}

		nl = new Layer(*m_layers.at(sourceIdx));
		nl->setTitle(name);
		nl->setId(id);

	} else {
		nl = new Layer(this, id, name, color, size());
	}

	// Insert the new layer in the appropriate spot
	int pos;
	if(insert)
		pos = sourceIdx+1;
	else
		pos = m_layers.size();

	m_layers.insert(pos, nl);

	// Dirty regions must be marked after the layer is in the stack
	if(copy)
		nl->markOpaqueDirty();
	else if(color.alpha()>0)
		markDirty();

	emit layerCreated(pos, nl->info());

	return nl;
}

/**
 * @param id layer ID
 * @return true if layer was found and deleted
 */
bool LayerStack::deleteLayer(int id)
{
	for(int i=0;i<m_layers.size();++i) {
		if(m_layers.at(i)->id() == id) {
			m_layers.at(i)->markOpaqueDirty();
			delete m_layers.takeAt(i);

			emit layerDeleted(i);

			return true;
		}
	}
	return false;
}

/**
 * @param neworder list of layer IDs in the new order
 * @pre neworder must be a permutation of the current layer order
 */
void LayerStack::reorderLayers(const QList<uint16_t> &neworder)
{
	Q_ASSERT(neworder.size() == m_layers.size());
	QList<Layer*> newstack;
	newstack.reserve(m_layers.size());
	for(const int id : neworder) {
		Layer *l = nullptr;
		for(int i=0;i<m_layers.size();++i) {
			if(m_layers.at(i)->id() == id) {
				l=m_layers.takeAt(i);
				break;
			}
		}
		Q_ASSERT(l);
		newstack.append(l);
	}
	m_layers = newstack;
	markDirty();

	emit layersChanged(layerInfos());
}

/**
 * @param id id of the layer that will be merged
 */
void LayerStack::mergeLayerDown(int id) {
	const Layer *top;
	Layer *btm=0;
	for(int i=0;i<m_layers.size();++i) {
		if(m_layers[i]->id() == id) {
			top = m_layers[i];
			if(i>0)
				btm = m_layers[i-1];
			break;
		}
	}
	if(btm==0)
		qWarning() << "Tried to merge bottom-most layer";
	else
		btm->merge(top);
}

Layer *LayerStack::getLayerByIndex(int index)
{
	return m_layers.at(index);
}

const Layer *LayerStack::getLayerByIndex(int index) const
{
	return m_layers.at(index);
}

Layer *LayerStack::getLayer(int id)
{
	// Since the expected number of layers is always fairly low,
	// we can get away with a simple linear search. (Note that IDs
	// may appear in random order due to layers being moved around.)
	for(Layer *l : m_layers)
		if(l->id() == id)
			return l;
	return 0;
}

const Layer *LayerStack::getLayer(int id) const
{
	for(const Layer *l : m_layers)
		if(l->id() == id)
			return l;
	return nullptr;
}

/**
 * @param id layer id
 * @return layer index. Returns a negative index if layer is not found
 */
int LayerStack::indexOf(int id) const
{
	for(int i=0;i<m_layers.size();++i)
		if(m_layers.at(i)->id() == id)
			return i;
	return -1;
}

namespace {

struct UpdateTile {
	UpdateTile() : x(-1), y(-1) {}
	UpdateTile(int x_, int y_) : x(x_), y(y_) {}

	int x, y;
	quint32 data[Tile::LENGTH];
};

}

/**
 * The dirty flag for each painted tile will be cleared.
 *
 * @param rect area of the image to limit repainting to (rounded upwards to tile boundaries)
 * @param target device to paint onto
 */
void LayerStack::paintChangedTiles(const QRect& rect, QPaintDevice *target, bool clean)
{
	if(m_width<=0 || m_height<=0)
		return;

	// Affected tile range
	const int tx0 = qBound(0, rect.left() / Tile::SIZE, m_xtiles-1);
	const int tx1 = qBound(tx0, rect.right() / Tile::SIZE, m_xtiles-1);
	const int ty0 = qBound(0, rect.top() / Tile::SIZE, m_ytiles-1);
	const int ty1 = qBound(ty0, rect.bottom() / Tile::SIZE, m_ytiles-1);

	// Gather list of tiles in need of updating
	QList<UpdateTile*> updates;

	for(int ty=ty0;ty<=ty1;++ty) {
		const int y = ty*m_xtiles;
		for(int tx=tx0;tx<=tx1;++tx) {
			const int i = y+tx;
			if(m_dirtytiles.testBit(i)) {
				updates.append(new UpdateTile(tx, ty));

				// TODO this conditional is for transitioning to QtQuick. Remove once old view is removed.
				if(clean)
					m_dirtytiles.clearBit(i);
			}
		}
	}

	if(!updates.isEmpty()) {
		// Flatten tiles
		concurrentForEach<UpdateTile*>(updates, [this](UpdateTile *t) {
			// TODO: don't draw the checkerboard here: use a QML item instead to draw the background
			Tile::fillChecker(t->data, QColor(128,128,128), Qt::white);
			flattenTile(t->data, t->x, t->y);
		});

		// Paint flattened tiles
		QPainter painter(target);
		painter.setCompositionMode(QPainter::CompositionMode_Source);
		while(!updates.isEmpty()) {
			UpdateTile *ut = updates.takeLast();
			painter.drawImage(
				ut->x*Tile::SIZE,
				ut->y*Tile::SIZE,
				QImage(reinterpret_cast<const uchar*>(ut->data),
					Tile::SIZE, Tile::SIZE,
					QImage::Format_ARGB32
				)
			);
			delete ut;
		}
	}
}

Tile LayerStack::getFlatTile(int x, int y) const
{
	Tile t(Qt::transparent);
	flattenTile(t.data(), x, y);
	return t;
}

const Layer *LayerStack::layerAt(int x, int y) const
{
	for(int i=m_layers.size()-1;i>=0;--i) {
		const Layer * l = m_layers.at(i);
		if(l->isVisible()) {
			if(qAlpha(l->pixelAt(x,y)) > 0)
				return l;

			// Check sublayers too
			for(const Layer *sl : l->sublayers()) {
				if(sl->isVisible() && qAlpha(sl->pixelAt(x, y)) > 0)
					return l;
			}
		}
	}
	return nullptr;
}

QColor LayerStack::colorAt(int x, int y, int dia) const
{
	if(m_layers.isEmpty())
		return QColor();

	if(x<0 || y<0 || x>=m_width || y>=m_height)
		return QColor();

	if(dia<=1) {
		// TODO some more efficient way of doing this
		Tile tile = getFlatTile(x/Tile::SIZE, y/Tile::SIZE);
		quint32 c = tile.data()[(y-Tile::roundDown(y)) * Tile::SIZE + (x-Tile::roundDown(x))];
		return QColor(c);

	} else {
		const int r = dia/2+1;
		const int x1 = (x-r) / Tile::SIZE;
		const int x2 = (x+r) / Tile::SIZE;
		const int y1 = (y-r) / Tile::SIZE;
		const int y2 = (y+r) / Tile::SIZE;

		Layer flat(nullptr, 0, QString(), Qt::transparent, size());

		for(int tx=x1;tx<=x2;++tx) {
			for(int ty=y1;ty<=y2;++ty) {
				flat.rtile(tx,ty) = getFlatTile(tx, ty);
			}
		}

		return flat.colorAt(x, y, dia);
	}
}

QImage LayerStack::toFlatImage(bool includeAnnotations) const
{
	if(m_layers.isEmpty())
		return QImage();

	QScopedPointer<Layer> flat { new Layer(*m_layers.at(0)) };

	for(int i=1;i<m_layers.size();++i) {
		if(m_layers.at(i)->isVisible())
			flat->merge(m_layers.at(i));
	}

	QImage image = flat->toImage();

	if(includeAnnotations) {
		QPainter painter(&image);
		for(const Annotation &a : m_annotations->getAnnotations())
			a.paint(&painter);
	}

	return image;
}

QImage LayerStack::flatLayerImage(int layerIdx, bool useBgLayer, const QColor &background) const
{
	Q_ASSERT(layerIdx>=0 && layerIdx < m_layers.size());

	QScopedPointer<Layer> flat;

	if(useBgLayer)
		flat.reset(new Layer(*m_layers.at(0)));
	else
		flat.reset(new Layer(nullptr, 0, QString(), background, size()));

	flat->merge(m_layers.at(layerIdx));

	return flat->toImage();
}

// Flatten a single tile
void LayerStack::flattenTile(quint32 *data, int xindex, int yindex) const
{
	// Composite visible layers
	int layeridx = 0;
	for(const Layer *l : m_layers) {
		if(isVisible(layeridx)) {
			const Tile &tile = l->tile(xindex, yindex);
			const quint32 tint = layerTint(layeridx);

			if(l->sublayers().count() || tint!=0) {
				// Sublayers (or tint) present, composite them first
				quint32 ldata[Tile::SIZE*Tile::SIZE];
				tile.copyTo(ldata);

				for(const Layer *sl : l->sublayers()) {
					if(sl->isVisible()) {
						const Tile &subtile = sl->tile(xindex, yindex);
						if(!subtile.isNull()) {
							compositePixels(sl->blendmode(), ldata, subtile.data(),
									Tile::SIZE*Tile::SIZE, sl->opacity());
						}
					}
				}

				if(tint)
					tintPixels(ldata, sizeof ldata / sizeof *ldata, tint);

				// Composite merged tile
				compositePixels(l->blendmode(), data, ldata,
						Tile::SIZE*Tile::SIZE, layerOpacity(layeridx));

			} else if(!tile.isNull()) {
				// No sublayers or tint, just this tile as it is
				compositePixels(l->blendmode(), data, tile.data(),
						Tile::SIZE*Tile::SIZE, layerOpacity(layeridx));
			}
		}

		++layeridx;
	}
}

void LayerStack::markDirty(const QRect &area)
{
	if(m_layers.isEmpty() || m_width<=0 || m_height<=0)
		return;
	const int tx0 = qBound(0, area.left() / Tile::SIZE, m_xtiles-1);
	const int tx1 = qBound(tx0, area.right() / Tile::SIZE, m_xtiles-1) + 1;
	int ty0 = qBound(0, area.top() / Tile::SIZE, m_ytiles-1);
	const int ty1 = qBound(ty0, area.bottom() / Tile::SIZE, m_ytiles-1);
	
	for(;ty0<=ty1;++ty0) {
		m_dirtytiles.fill(true, ty0*m_xtiles + tx0, ty0*m_xtiles + tx1);
	}
	m_dirtyrect |= area;
}

void LayerStack::markDirty()
{
	if(m_layers.isEmpty() || m_width<=0 || m_height<=0)
		return;
	m_dirtytiles.fill(true);

	m_dirtyrect = QRect(0, 0, m_width, m_height);
	notifyAreaChanged();
}

void LayerStack::markDirty(int x, int y)
{
	Q_ASSERT(x>=0 && x < m_xtiles);
	Q_ASSERT(y>=0 && y < m_ytiles);

	m_dirtytiles.setBit(y*m_xtiles + x);

	m_dirtyrect |= QRect(x*Tile::SIZE, y*Tile::SIZE, Tile::SIZE, Tile::SIZE);
}

void LayerStack::markDirty(int index)
{
	Q_ASSERT(index>=0 && index < m_dirtytiles.size());

	m_dirtytiles.setBit(index);

	const int y = index / m_xtiles;
	const int x = index % m_xtiles;

	m_dirtyrect |= QRect(x*Tile::SIZE, y*Tile::SIZE, Tile::SIZE, Tile::SIZE);
}

void LayerStack::notifyAreaChanged()
{
	if(!m_dirtyrect.isEmpty()) {
		emit areaChanged(m_dirtyrect);
		m_dirtyrect = QRect();
	}
}

void LayerStack::notifyLayerInfoChange(const Layer *layer)
{
	Q_ASSERT(layer);
	// Note: Sublayes changes can trigger, but will not be found
	// by indexOf. That's OK, since sublayers are for internal use
	// only and we don't need to announce changes to them.
	const int idx = indexOf(layer->id());
	if(idx>=0) {
		emit layerChanged(idx);
	}
}

void LayerStack::setViewMode(ViewMode mode)
{
	if(mode != m_viewmode) {
		m_viewmode = mode;
		markDirty();
	}
}

void LayerStack::setViewLayer(int id)
{
	for(int i=0;i<m_layers.size();++i) {
		if(m_layers.at(i)->id() == id) {
			m_viewlayeridx = i;
			if(m_viewmode != NORMAL)
				markDirty();
			break;
		}
	}
}

void LayerStack::setOnionskinMode(int below, int above, bool tint)
{
	m_onionskinsBelow = below;
	m_onionskinsAbove = above;
	m_onionskinTint = tint;

	if(m_viewmode==ONIONSKIN)
		markDirty();
}

void LayerStack::setViewBackgroundLayer(bool usebg)
{
	m_viewBackgroundLayer = usebg;
	if(m_viewmode != NORMAL)
		markDirty();
}

int LayerStack::layerOpacity(int idx) const
{
	Q_ASSERT(idx>=0 && idx < m_layers.size());
	int o = m_layers.at(idx)->opacity();

	if(m_viewBackgroundLayer && idx==0)
		return o;

	if(viewMode()==ONIONSKIN) {
		const int d = m_viewlayeridx - idx;
		qreal rd;
		if(d<0 && d>=-m_onionskinsAbove)
			rd = -d/qreal(m_onionskinsAbove+1);
		else if(d>=0 && d <=m_onionskinsBelow)
			rd = d/qreal(m_onionskinsBelow+1);
		else
			return 0;

		return int(o * ((1-rd) * (1-rd)));
	}

	return o;
}

quint32 LayerStack::layerTint(int idx) const
{
	if(m_onionskinTint && viewMode() == ONIONSKIN) {
		if(idx==0 && m_viewBackgroundLayer)
			return 0;

		if(idx < m_viewlayeridx)
			return 0x80ff3333;
		else if(idx > m_viewlayeridx)
			return 0x803333ff;
	}

	return 0;
}

bool LayerStack::isVisible(int idx) const
{
	Q_ASSERT(idx>=0 && idx < m_layers.size());
	if(!m_layers.at(idx)->isVisible())
		return false;

	if(idx==0 && m_viewBackgroundLayer)
		return true;
	switch(viewMode()) {
	case NORMAL: break;
	case SOLO: return idx == m_viewlayeridx;
	case ONIONSKIN: return layerOpacity(idx) > 0;
	}

	return true;
}

Savepoint::~Savepoint()
{
	while(!layers.isEmpty())
		delete layers.takeLast();
}

Savepoint *LayerStack::makeSavepoint()
{
	Savepoint *sp = new Savepoint;
	for(Layer *l : m_layers) {
		l->optimize();
		sp->layers.append(new Layer(*l));
	}

	sp->annotations = m_annotations->getAnnotations();

	sp->width = m_width;
	sp->height = m_height;

	return sp;
}

void LayerStack::restoreSavepoint(const Savepoint *savepoint)
{
	const QSize oldsize(m_width, m_height);
	if(m_width != savepoint->width || m_height != savepoint->height) {
		// Restore canvas size if it was different in the savepoint
		m_width = savepoint->width;
		m_height = savepoint->height;
		m_xtiles = Tile::roundTiles(m_width);
		m_ytiles = Tile::roundTiles(m_height);
		m_dirtytiles = QBitArray(m_xtiles*m_ytiles, true);
		emit resized(0, 0, oldsize);

	} else {
		// Mark changed tiles as changed. Usually savepoints are quite close together
		// so most tiles will remain unchanged
		if(savepoint->layers.size() != m_layers.size()) {
			// Layers added or deleted, just refresh everything
			// (force refresh even if layer stack is empty)
			m_dirtytiles.fill(true);
			m_dirtyrect = QRect(0, 0, m_width, m_height);

		} else {
			// Layer count has not changed, compare layer contents
			for(int l=0;l<savepoint->layers.size();++l) {
				const Layer *l0 = m_layers.at(l);
				const Layer *l1 = savepoint->layers.at(l);
				if(l0->effectiveOpacity() != l1->effectiveOpacity()) {
					// Layer opacity has changed, refresh everything
					markDirty();
					break;
				}
				for(int i=0;i<m_xtiles*m_ytiles;++i) {
					// Note: An identity comparison works here, because the tiles
					// utilize copy-on-write semantics. Unchanged tiles will share
					// data pointers between savepoints.
					if(l0->tile(i) != l1->tile(i))
						markDirty(i);
				}
			}
		}
	}

	// Restore layers
	while(!m_layers.isEmpty())
		delete m_layers.takeLast();
	for(const Layer *l : savepoint->layers)
		m_layers.append(new Layer(*l));

	// Restore annotations
	m_annotations->setAnnotations(savepoint->annotations);

	notifyAreaChanged();
	emit layersChanged(layerInfos());
}

QList<LayerInfo> LayerStack::layerInfos() const
{
	QList<LayerInfo> infos;
	infos.reserve(m_layers.size());
	for(const Layer *l : m_layers)
		infos << l->info();
	return infos;
}

void Savepoint::toDatastream(QDataStream &out) const
{
	// Write size
	out << quint32(width) << quint32(height);

	// Write layers
	out << quint8(layers.size());
	for(const Layer *layer : layers) {
		layer->toDatastream(out);
	}

	// Write annotations
	out << quint16(annotations.size());
	for(const Annotation &annotation : annotations) {
		annotation.toDataStream(out);
	}
}

Savepoint *Savepoint::fromDatastream(QDataStream &in, LayerStack *owner)
{
	Savepoint *sp = new Savepoint;
	quint32 width, height;
	in >> width >> height;

	sp->width = width;
	sp->height = height;

	quint8 layers;
	in >> layers;
	while(layers--) {
		sp->layers.append(Layer::fromDatastream(owner, in));
	}

	quint16 annotations;
	in >> annotations;
	while(annotations--) {
		sp->annotations.append(Annotation::fromDataStream(in));
	}

	return sp;
}

}

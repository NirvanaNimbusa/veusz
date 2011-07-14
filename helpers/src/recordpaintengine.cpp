//    Copyright (C) 2011 Jeremy S. Sanders
//    Email: Jeremy Sanders <jeremy@jeremysanders.net>
//
//    This program is free software; you can redistribute it and/or modify
//    it under the terms of the GNU General Public License as published by
//    the Free Software Foundation; either version 2 of the License, or
//    (at your option) any later version.
//
//    This program is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU General Public License for more details.
//
//    You should have received a copy of the GNU General Public License along
//    with this program; if not, write to the Free Software Foundation, Inc.,
//    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
/////////////////////////////////////////////////////////////////////////////

#include <QPainter>
#include <QImage>
#include <QRectF>
#include <QLineF>
#include <QVector>
#include <QPaintEngine>

#include "paintelement.h"
#include "recordpaintengine.h"
#include "recordpaintdevice.h"

namespace {

  //////////////////////////////////////////////////////////////
  // Drawing Elements
  // these are defined for each type of painting 
  // the QPaintEngine does

  // draw an ellipse
  class EllipseElement : public PaintElement {
  public:
    EllipseElement(const QRectF &rect) : _ellipse(rect) {}

    void paint(QPainter& painter)
    {
      painter.drawEllipse(_ellipse);
    }

  private:
    QRectF _ellipse;
  };

  // draw QImage
  class ImageElement : public PaintElement {
  public:
    ImageElement(const QRectF& rect, const QImage& image,
		 const QRectF& sr, Qt::ImageConversionFlags flags)
      : _image(image), _rect(rect), _sr(sr), _flags(flags)
    {}

    void paint(QPainter& painter)
    {
      painter.drawImage(_rect, _image, _sr, _flags);
    }

  private:
    QImage _image;
    QRectF _rect, _sr;
    Qt::ImageConversionFlags _flags;
  };

  // draw lines
  // this is for painting QLine and QLineF
  template <class T>
  class lineElement : public PaintElement {
  public:
    lineElement(const T *lines, int linecount)
    {
      for(int i = 0; i < linecount; i++)
	_lines << lines[i];
    }

    void paint(QPainter& painter)
    {
      painter.drawLines(_lines);
    }

  private:
    QVector<T> _lines;
  };
  // specific Line and LineF variants
  typedef lineElement<QLine> LineElement;
  typedef lineElement<QLineF> LineFElement;

  // draw QPainterPath
  class PathElement : public PaintElement {
  public:
    PathElement(const QPainterPath& path)
      : _path(path) {}

    void paint(QPainter& painter)
    {
      painter.drawPath(_path);
    }

  private:
    QPainterPath _path;
  };

  // draw Pixmap
  class PixmapElement : public PaintElement {
  public:
    PixmapElement(const QRectF& r, const QPixmap& pm,
		  const QRectF& sr) :
      _r(r), _pm(pm), _sr(sr) {}

    void paint(QPainter& painter)
    {
      painter.drawPixmap(_r, _pm, _sr);
    }

  private:
    QRectF _r;
    QPixmap _pm;
    QRectF _sr;
  };

  // draw points (QPoint and QPointF)
  template <class T, class V>
  class pointElement : public PaintElement {
  public:
    pointElement(const T* points, int pointcount)
    {
      for(int i=0; i<pointcount; ++i)
	_pts << points[i];
    }

    void paint(QPainter& painter)
    {
      painter.drawPoints(_pts);
    }

  private:
    V _pts;
  };
  typedef pointElement<QPoint, QPolygon> PointElement;
  typedef pointElement<QPointF, QPolygonF> PointFElement;

  // for QPolygon and QPolygonF
  template <class T, class V>
  class polyElement: public PaintElement {
  public:
    polyElement(const T* points, int pointcount,
		QPaintEngine::PolygonDrawMode mode)
      : _mode(mode)
    {
      for(int i=0; i<pointcount; ++i)
	_pts << points[i];
    }

    void paint(QPainter& painter)
    {
      switch(_mode)
	{
	case QPaintEngine::OddEvenMode:
	  painter.drawPolygon(_pts, Qt::OddEvenFill);
	  break;
	case QPaintEngine::WindingMode:
	  painter.drawPolygon(_pts, Qt::WindingFill);
	  break;
	case QPaintEngine::ConvexMode:
	  painter.drawConvexPolygon(_pts);
	  break;
	case QPaintEngine::PolylineMode:
	  painter.drawPolyline(_pts);
	  break;
	}
    }

  private:
    QPaintEngine::PolygonDrawMode _mode;
    V _pts;
  };
  typedef polyElement<QPoint,QPolygon> PolygonElement;
  typedef polyElement<QPointF,QPolygonF> PolygonFElement;

  // for QRect and QRectF
  template <class T>
  class rectElement : public PaintElement {
  public:
    rectElement(const T* rects, int rectcount)
    {
      for(int i=0; i<rectcount; i++)
	_rects << rects[i];
    }

    void paint(QPainter& painter)
    {
      painter.drawRects(_rects);
    }

  private:
    QVector<T> _rects;
  };
  typedef rectElement<QRect> RectElement;
  typedef rectElement<QRectF> RectFElement;

  // draw Text
  class TextElement : public PaintElement {
  public:
    TextElement(const QPointF& pt, const QTextItem& txt)
      : _pt(pt), _text(txt.text())
    {}

    void paint(QPainter& painter)
    {
      painter.drawText(_pt, _text);
    }

  private:
    QPointF _pt;
    QString _text;
  };

  class TiledPixmapElement : public PaintElement {
  public:
    TiledPixmapElement(const QRectF& rect, const QPixmap& pixmap,
		       const QPointF& pt)
      : _rect(rect), _pixmap(pixmap), _pt(pt)
    {}

    void paint(QPainter& painter)
    {
      painter.drawTiledPixmap(_rect, _pixmap, _pt);
    }

  private:
    QRectF _rect;
    QPixmap _pixmap;
    QPointF _pt;
  };

  ///////////////////////////////////////////////////////////////////
  // State paint elements

  // these define and change the state of the painter

  class BackgroundBrushElement : public PaintElement {
  public:
    BackgroundBrushElement(const QBrush& brush)
      : _brush(brush)
    {}

    void paint(QPainter& painter)
    {
      painter.setBackground(_brush);
    }

  private:
    QBrush _brush;
  };

  class BackgroundModeElement : public PaintElement {
  public:
    BackgroundModeElement(Qt::BGMode mode)
      : _mode(mode)
    {}

    void paint(QPainter& painter)
    {
      painter.setBackgroundMode(_mode);
    }

  private:
    Qt::BGMode _mode;
  };

  class BrushElement : public PaintElement {
  public:
    BrushElement(const QBrush& brush)
      : _brush(brush)
    {}

    void paint(QPainter& painter)
    {
      painter.setBrush(_brush);
    }

  private:
    QBrush _brush;
  };

  class BrushOriginElement : public PaintElement {
  public:
    BrushOriginElement(const QPointF& origin)
      : _origin(origin)
    {}

    void paint(QPainter& painter)
    {
      painter.setBrushOrigin(_origin);
    }

  private:
    QPointF _origin;
  };

  class ClipRegionElement : public PaintElement {
  public:
    ClipRegionElement(Qt::ClipOperation op,
		      const QRegion& region)
      : _op(op), _region(region)
    {}

    void paint(QPainter& painter)
    {
      painter.setClipRegion(_region, _op);
    }

  private:
    Qt::ClipOperation _op;
    QRegion _region;
  };

  class ClipPathElement : public PaintElement {
  public:
    ClipPathElement(Qt::ClipOperation op,
		    const QPainterPath& region)
      : _op(op), _region(region)
    {}

    void paint(QPainter& painter)
    {
      painter.setClipPath(_region, _op);
    }

  private:
    Qt::ClipOperation _op;
    QPainterPath _region;
  };

  class CompositionElement : public PaintElement {
  public:
    CompositionElement(QPainter::CompositionMode mode)
      : _mode(mode)
    {}

    void paint(QPainter& painter)
    {
      painter.setCompositionMode(_mode);
    }

  private:
    QPainter::CompositionMode _mode;
  };

  class FontElement : public PaintElement {
  public:
    FontElement(const QFont& font)
      : _font(font)
    {}

    void paint(QPainter& painter)
    {
      painter.setFont(_font);
    }

  private:
    QFont _font;
  };

  class TransformElement : public PaintElement {
  public:
    TransformElement(const QTransform& t)
      : _t(t)
    {}

    void paint(QPainter& painter)
    {
      painter.setWorldTransform(_t);
    }

  private:
    QTransform _t;
  };

  class ClipEnabledElement : public PaintElement {
  public:
    ClipEnabledElement(bool enabled)
      : _enabled(enabled)
    {}

    void paint(QPainter& painter)
    {
      painter.setClipping(_enabled);
    }

  private:
    bool _enabled;
  };

  class PenElement : public PaintElement {
  public:
    PenElement(const QPen& pen)
      : _pen(pen)
    {}

    void paint(QPainter& painter)
    {
      painter.setPen(_pen);
    }

  private:
    QPen _pen;
  };

  class HintsElement : public PaintElement {
  public:
    HintsElement(QPainter::RenderHints hints)
      : _hints(hints)
    {}

    void paint(QPainter& painter)
    {
      painter.setRenderHints(_hints);
    }

  private:
    QPainter::RenderHints _hints;
  };


  // end anonymous block
}

///////////////////////////////////////////////////////////////////
// Paint engine follows

RecordPaintEngine::RecordPaintEngine()
  : QPaintEngine(QPaintEngine::AllFeatures),
    _pdev(0)
{
}

bool RecordPaintEngine::begin(QPaintDevice* pdev)
{
  // old style C cast - probably should use dynamic_cast
  _pdev = (RecordPaintDevice*)(pdev);

  // signal started ok
  return 1;
}

// for each type of drawing command we add a new element
// to the list maintained by the device

void RecordPaintEngine::drawEllipse(const QRectF& rect)
{
  _pdev->addElement( new EllipseElement(rect) );
}

void RecordPaintEngine::drawEllipse(const QRect& rect)
{
  _pdev->addElement( new EllipseElement(rect) );
}

void RecordPaintEngine::drawImage(const QRectF& rectangle,
				  const QImage& image,
				  const QRectF& sr,
				  Qt::ImageConversionFlags flags)
{
  _pdev->addElement( new ImageElement(rectangle, image, sr, flags) );
}

void RecordPaintEngine::drawLines(const QLineF* lines, int lineCount)
{
  _pdev->addElement( new LineFElement(lines, lineCount) );
}

void RecordPaintEngine::drawLines(const QLine* lines, int lineCount)
{
  _pdev->addElement( new LineElement(lines, lineCount) );
}

void RecordPaintEngine::drawPath(const QPainterPath& path)
{
  _pdev->addElement( new PathElement(path) );
}

void RecordPaintEngine::drawPixmap(const QRectF& r,
					 const QPixmap& pm, const QRectF& sr)
{
  _pdev->addElement( new PixmapElement(r, pm, sr) );
}

void RecordPaintEngine::drawPoints(const QPointF* points, int pointCount)
{
  _pdev->addElement( new PointFElement(points, pointCount) );
}

void RecordPaintEngine::drawPoints(const QPoint* points, int pointCount)
{
  _pdev->addElement( new PointElement(points, pointCount) );
}

void RecordPaintEngine::drawPolygon(const QPointF* points, int pointCount,
					  QPaintEngine::PolygonDrawMode mode)
{
  _pdev->addElement( new PolygonFElement(points, pointCount, mode) );
}

void RecordPaintEngine::drawPolygon(const QPoint* points, int pointCount,
					  QPaintEngine::PolygonDrawMode mode)
{
  _pdev->addElement( new PolygonElement(points, pointCount, mode) );
}

void RecordPaintEngine::drawRects(const QRectF* rects, int rectCount)
{
  _pdev->addElement( new RectFElement( rects, rectCount ) );
}

void RecordPaintEngine::drawRects(const QRect* rects, int rectCount)
{
  _pdev->addElement( new RectElement( rects, rectCount ) );
}

void RecordPaintEngine::drawTextItem(const QPointF& p,
					   const QTextItem& textItem)
{
  _pdev->addElement( new TextElement(p, textItem) );
}

void RecordPaintEngine::drawTiledPixmap(const QRectF& rect,
					      const QPixmap& pixmap,
					      const QPointF& p)
{
  _pdev->addElement( new TiledPixmapElement(rect, pixmap, p) );
}

bool RecordPaintEngine::end()
{
  // signal finished ok
  return 1;
}

QPaintEngine::Type RecordPaintEngine::type () const
{
  // some sort of random number for the ID of the engine type
  return QPaintEngine::Type(int(QPaintEngine::User)+34);
}

void RecordPaintEngine::updateState(const QPaintEngineState& state)
{
  // we add a new element for each change of state
  // these are replayed later
  const int flags = state.state();
  if( flags & QPaintEngine::DirtyBackground )
    _pdev->addElement( new BackgroundBrushElement( state.backgroundBrush() ) );
  if( flags & QPaintEngine::DirtyBackgroundMode )
    _pdev->addElement( new BackgroundModeElement( state.backgroundMode() ) );
  if( flags & QPaintEngine::DirtyBrush )
    _pdev->addElement( new BrushElement( state.brush() ) );
  if( flags & QPaintEngine::DirtyBrushOrigin )
    _pdev->addElement( new BrushOriginElement( state.brushOrigin() ) );
  if( flags & QPaintEngine::DirtyClipRegion )
    _pdev->addElement( new ClipRegionElement( state.clipOperation(),
					      state.clipRegion() ) );
  if( flags & QPaintEngine::DirtyClipPath )
    _pdev->addElement( new ClipPathElement( state.clipOperation(),
					    state.clipPath() ) );
  if( flags & QPaintEngine::DirtyCompositionMode )
    _pdev->addElement( new CompositionElement( state.compositionMode() ) );
  if( flags & QPaintEngine::DirtyFont )
    _pdev->addElement( new FontElement( state.font() ) );
  if( flags & QPaintEngine::DirtyTransform )
    _pdev->addElement( new TransformElement( state.transform() ) );
  if( flags & QPaintEngine::DirtyClipEnabled )
    _pdev->addElement( new ClipEnabledElement( state.isClipEnabled() ) );
  if( flags & QPaintEngine::DirtyPen )
    _pdev->addElement( new PenElement( state.pen() ) );
  if( flags & QPaintEngine::DirtyHints )
    _pdev->addElement( new HintsElement( state.renderHints() ) );
}
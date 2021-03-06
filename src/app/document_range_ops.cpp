/* Aseprite
 * Copyright (C) 2001-2014  David Capello
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "app/document_range_ops.h"

#include "app/app.h"            // TODO remove this dependency
#include "app/context_access.h"
#include "app/document_api.h"
#include "app/document_range.h"
#include "app/undo_transaction.h"
#include "doc/layer.h"
#include "doc/sprite.h"

#include <stdexcept>

namespace app {

enum Op { Move, Copy };

static DocumentRange drop_range_op(
  Document* doc, Op op, const DocumentRange& from,
  DocumentRangePlace place, const DocumentRange& to)
{
  Sprite* sprite = doc->sprite();

  // Check noop/trivial/do nothing cases, i.e., move a range to the same place.
  // Also check invalid cases, like moving a Background layer.
  switch (from.type()) {
    case DocumentRange::kCels:
      if (from == to)
        return from;
      break;
    case DocumentRange::kFrames:
      if (op == Move) {
        if ((to.frameBegin() >= from.frameBegin() && to.frameEnd() <= from.frameEnd()) ||
            (place == kDocumentRangeBefore && to.frameBegin() == from.frameEnd()+1) ||
            (place == kDocumentRangeAfter && to.frameEnd() == from.frameBegin()-1))
          return from;
      }
      break;
    case DocumentRange::kLayers:
      if (op == Move) {
        if ((to.layerBegin() >= from.layerBegin() && to.layerEnd() <= from.layerEnd()) ||
            (place == kDocumentRangeBefore && to.layerBegin() == from.layerEnd()+1) ||
            (place == kDocumentRangeAfter && to.layerEnd() == from.layerBegin()-1))
          return from;

        // We cannot move the background
        for (LayerIndex i = from.layerBegin(); i <= from.layerEnd(); ++i)
          if (sprite->indexToLayer(i)->isBackground())
            throw std::runtime_error("The background layer cannot be moved");

        // Before background
        if (place == kDocumentRangeBefore) {
          Layer* background = sprite->indexToLayer(to.layerBegin());
          if (background && background->isBackground())
            throw std::runtime_error("You cannot move something below the background layer");
        }
      }
      break;
  }

  const char* undoLabel = NULL;
  switch (op) {
    case Move: undoLabel = "Move Range"; break;
    case Copy: undoLabel = "Copy Range"; break;
  }
  DocumentRange resultRange;

  {
    const app::Context* context = static_cast<app::Context*>(doc->context());
    const ContextReader reader(context);
    ContextWriter writer(reader);
    UndoTransaction undo(writer.context(), undoLabel, undo::ModifyDocument);
    DocumentApi api = doc->getApi();

    // TODO Try to add the range with just one call to DocumentApi
    // methods, to avoid generating a lot of SetCelFrame undoers (see
    // DocumentApi::setCelFramePosition).

    switch (from.type()) {

      case DocumentRange::kCels:
        {
          std::vector<Layer*> layers;
          sprite->getLayersList(layers);

          int srcLayerBegin, srcLayerStep, srcLayerEnd;
          int dstLayerBegin, dstLayerStep;
          FrameNumber srcFrameBegin, srcFrameStep, srcFrameEnd;
          FrameNumber dstFrameBegin, dstFrameStep;

          if (to.layerBegin() <= from.layerBegin()) {
            srcLayerBegin = from.layerBegin();
            srcLayerStep = 1;
            srcLayerEnd = from.layerEnd()+1;
            dstLayerBegin = to.layerBegin();
            dstLayerStep = 1;
          }
          else {
            srcLayerBegin = from.layerEnd();
            srcLayerStep = -1;
            srcLayerEnd = from.layerBegin()-1;
            dstLayerBegin = to.layerEnd();
            dstLayerStep = -1;
          }

          if (to.frameBegin() <= from.frameBegin()) {
            srcFrameBegin = from.frameBegin();
            srcFrameStep = FrameNumber(1);
            srcFrameEnd = from.frameEnd().next();
            dstFrameBegin = to.frameBegin();
            dstFrameStep = FrameNumber(1);
          }
          else {
            srcFrameBegin = from.frameEnd();
            srcFrameStep = FrameNumber(-1);
            srcFrameEnd = from.frameBegin().previous();
            dstFrameBegin = to.frameEnd();
            dstFrameStep = FrameNumber(-1);
          }

          for (int srcLayerIdx = srcLayerBegin,
                 dstLayerIdx = dstLayerBegin; srcLayerIdx != srcLayerEnd; ) {
            for (FrameNumber srcFrame = srcFrameBegin,
                   dstFrame = dstFrameBegin; srcFrame != srcFrameEnd; ) {
              LayerImage* srcLayer = static_cast<LayerImage*>(layers[srcLayerIdx]);
              LayerImage* dstLayer = static_cast<LayerImage*>(layers[dstLayerIdx]);

              switch (op) {
                case Move: api.moveCel(srcLayer, srcFrame, dstLayer, dstFrame); break;
                case Copy: api.copyCel(srcLayer, srcFrame, dstLayer, dstFrame); break;
              }

              srcFrame += srcFrameStep;
              dstFrame += dstFrameStep;
            }
            srcLayerIdx += srcLayerStep;
            dstLayerIdx += dstLayerStep;
          }

          resultRange = to;
        }
        break;

      case DocumentRange::kFrames:
        {
          FrameNumber srcFrameBegin, srcFrameStep, srcFrameEnd;
          FrameNumber dstFrameBegin, dstFrameStep;

          switch (op) {

            case Move:
              if (place == kDocumentRangeBefore) {
                if (to.frameBegin() <= from.frameBegin()) {
                  srcFrameBegin = from.frameBegin();
                  srcFrameStep = FrameNumber(1);
                  srcFrameEnd = from.frameEnd().next();
                  dstFrameBegin = to.frameBegin();
                  dstFrameStep = FrameNumber(1);
                }
                else {
                  srcFrameBegin = from.frameEnd();
                  srcFrameStep = FrameNumber(-1);
                  srcFrameEnd = from.frameBegin().previous();
                  dstFrameBegin = to.frameBegin();
                  dstFrameStep = FrameNumber(-1);
                }
              }
              else if (place == kDocumentRangeAfter) {
                if (to.frameEnd() <= from.frameBegin()) {
                  srcFrameBegin = from.frameBegin();
                  srcFrameStep = FrameNumber(1);
                  srcFrameEnd = from.frameEnd().next();
                  dstFrameBegin = to.frameEnd();
                  dstFrameStep = FrameNumber(1);
                }
                else {
                  srcFrameBegin = from.frameEnd();
                  srcFrameStep = FrameNumber(-1);
                  srcFrameEnd = from.frameBegin().previous();
                  dstFrameBegin = to.frameEnd().next();
                  dstFrameStep = FrameNumber(-1);
                }
              }
              break;

            case Copy:
              if (place == kDocumentRangeBefore) {
                if (to.frameBegin() <= from.frameBegin()) {
                  srcFrameBegin = from.frameBegin();
                  srcFrameStep = FrameNumber(2);
                  srcFrameEnd = from.frameBegin().next(2*from.frames());
                  dstFrameBegin = to.frameBegin();
                  dstFrameStep = FrameNumber(1);
                }
                else {
                  srcFrameBegin = from.frameEnd();
                  srcFrameStep = FrameNumber(-1);
                  srcFrameEnd = from.frameBegin().previous();
                  dstFrameBegin = to.frameBegin();
                  dstFrameStep = FrameNumber(0);
                }
              }
              else if (place == kDocumentRangeAfter) {
                if (to.frameEnd() <= from.frameBegin()) {
                  srcFrameBegin = from.frameBegin();
                  srcFrameStep = FrameNumber(2);
                  srcFrameEnd = from.frameBegin().next(2*from.frames());
                  dstFrameBegin = to.frameEnd().next();
                  dstFrameStep = FrameNumber(1);
                }
                else {
                  srcFrameBegin = from.frameEnd();
                  srcFrameStep = FrameNumber(-1);
                  srcFrameEnd = from.frameBegin().previous();
                  dstFrameBegin = to.frameEnd().next();
                  dstFrameStep = FrameNumber(0);
                }
              }
              break;
          }

          for (FrameNumber srcFrame = srcFrameBegin,
                 dstFrame = dstFrameBegin; srcFrame != srcFrameEnd; ) {
            switch (op) {
              case Move: api.moveFrame(sprite, srcFrame, dstFrame); break;
              case Copy: api.copyFrame(sprite, srcFrame, dstFrame); break;
            }
            srcFrame += srcFrameStep;
            dstFrame += dstFrameStep;
          }

          if (place == kDocumentRangeBefore) {
            resultRange.startRange(LayerIndex::NoLayer, FrameNumber(to.frameBegin()), from.type());
            resultRange.endRange(LayerIndex::NoLayer, FrameNumber(to.frameBegin()+from.frames()-1));
          }
          else if (place == kDocumentRangeAfter) {
            resultRange.startRange(LayerIndex::NoLayer, FrameNumber(to.frameEnd()+1), from.type());
            resultRange.endRange(LayerIndex::NoLayer, FrameNumber(to.frameEnd()+1+from.frames()-1));
          }

          if (op == Move && from.frameBegin() < to.frameBegin())
            resultRange.displace(0, -from.frames());
        }
        break;

      case DocumentRange::kLayers:
        {
          std::vector<Layer*> layers;
          sprite->getLayersList(layers);

          if (layers.empty())
            break;

          switch (op) {

            case Move:
              if (place == kDocumentRangeBefore) {
                for (LayerIndex i = from.layerBegin(); i <= from.layerEnd(); ++i) {
                  api.restackLayerBefore(
                    layers[i],
                    layers[to.layerBegin()]);
                }
              }
              else if (place == kDocumentRangeAfter) {
                for (LayerIndex i = from.layerEnd(); i >= from.layerBegin(); --i) {
                  api.restackLayerAfter(
                    layers[i],
                    layers[to.layerEnd()]);
                }
              }
              break;

            case Copy:
              if (place == kDocumentRangeBefore) {
                for (LayerIndex i = from.layerBegin(); i <= from.layerEnd(); ++i) {
                  api.duplicateLayerBefore(
                    layers[i],
                    layers[to.layerBegin()]);
                }
              }
              else if (place == kDocumentRangeAfter) {
                for (LayerIndex i = from.layerEnd(); i >= from.layerBegin(); --i) {
                  api.duplicateLayerAfter(
                    layers[i],
                    layers[to.layerEnd()]);
                }
              }
              break;
          }

          if (place == kDocumentRangeBefore) {
            resultRange.startRange(LayerIndex(to.layerBegin()), FrameNumber(-1), from.type());
            resultRange.endRange(LayerIndex(to.layerBegin()+from.layers()-1), FrameNumber(-1));
          }
          else if (place == kDocumentRangeAfter) {
            resultRange.startRange(LayerIndex(to.layerEnd()+1), FrameNumber(-1), from.type());
            resultRange.endRange(LayerIndex(to.layerEnd()+1+from.layers()-1), FrameNumber(-1));
          }

          if (op == Move && from.layerBegin() < to.layerBegin())
            resultRange.displace(-from.layers(), 0);
        }
        break;
    }

    undo.commit();
  }

  return resultRange;
}

DocumentRange move_range(Document* doc, const DocumentRange& from, const DocumentRange& to, DocumentRangePlace place)
{
  return drop_range_op(doc, Move, from, place, to);
}

DocumentRange copy_range(Document* doc, const DocumentRange& from, const DocumentRange& to, DocumentRangePlace place)
{
  return drop_range_op(doc, Copy, from, place, to);
}

void reverse_frames(Document* doc, const DocumentRange& range)
{
  const app::Context* context = static_cast<app::Context*>(doc->context());
  const ContextReader reader(context);
  ContextWriter writer(reader);
  UndoTransaction undo(writer.context(), "Reverse Frames");
  DocumentApi api = doc->getApi();
  Sprite* sprite = doc->sprite();
  FrameNumber frameBegin, frameEnd;
  int layerBegin, layerEnd;
  bool moveFrames = false;

  switch (range.type()) {
    case DocumentRange::kCels:
      frameBegin = range.frameBegin();
      frameEnd = range.frameEnd();
      layerBegin = range.layerBegin();
      layerEnd = range.layerEnd() + 1;
      break;
    case DocumentRange::kFrames:
      frameBegin = range.frameBegin();
      frameEnd = range.frameEnd();
      moveFrames = true;
      break;
    case DocumentRange::kLayers:
      frameBegin = FrameNumber(0);
      frameEnd = sprite->totalFrames().previous();
      layerBegin = range.layerBegin();
      layerEnd = range.layerEnd() + 1;
      break;
  }

  if (moveFrames) {
    for (FrameNumber frameRev = frameEnd.next();
         frameRev > frameBegin;
         frameRev = frameRev.previous()) {
      api.moveFrame(sprite, frameBegin, frameRev);
    }
  }
  else {
    std::vector<Layer*> layers;
    sprite->getLayersList(layers);

    for (int layerIdx = layerBegin; layerIdx != layerEnd; ++layerIdx) {
      for (FrameNumber frame = frameBegin,
             frameRev = frameEnd;
           frame != FrameNumber((frameBegin+frameEnd)/2).next();
           frame = frame.next(),
             frameRev = frameRev.previous()) {
        LayerImage* layer = static_cast<LayerImage*>(layers[layerIdx]);
        api.swapCel(layer, frame, frameRev);
      }
    }
  }

  undo.commit();
}

} // namespace app

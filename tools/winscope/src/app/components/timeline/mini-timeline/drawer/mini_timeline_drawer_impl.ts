/*
 * Copyright (C) 2022 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

import {Color} from 'app/colors';
import {TRACE_INFO} from 'app/trace_info';
import {Point} from 'common/geometry_types';
import {Padding} from 'common/padding';
import {Timestamp} from 'common/time';
import {CanvasMouseHandler} from './canvas_mouse_handler';
import {CanvasMouseHandlerImpl} from './canvas_mouse_handler_impl';
import {DraggableCanvasObject} from './draggable_canvas_object';
import {DraggableCanvasObjectImpl} from './draggable_canvas_object_impl';
import {MiniCanvasDrawerData, TimelineEntries} from './mini_canvas_drawer_data';
import {MiniTimelineDrawer} from './mini_timeline_drawer';
import {MiniTimelineDrawerInput} from './mini_timeline_drawer_input';

/**
 * Mini timeline drawer implementation
 * @docs-private
 */
export class MiniTimelineDrawerImpl implements MiniTimelineDrawer {
  ctx: CanvasRenderingContext2D;
  handler: CanvasMouseHandler;
  private activePointer: DraggableCanvasObject;

  getXScale() {
    return this.ctx.getTransform().m11;
  }

  getYScale() {
    return this.ctx.getTransform().m22;
  }

  getWidth() {
    return this.canvas.width / this.getXScale();
  }

  getHeight() {
    return this.canvas.height / this.getYScale();
  }

  getUsableRange() {
    const padding = this.getPadding();
    return {
      from: padding.left,
      to: this.getWidth() - padding.left - padding.right,
    };
  }

  getInput(): MiniCanvasDrawerData {
    return this.inputGetter().transform(this.getUsableRange());
  }

  constructor(
    public canvas: HTMLCanvasElement,
    private inputGetter: () => MiniTimelineDrawerInput,
    private onPointerPositionDragging: (pos: Timestamp) => void,
    private onPointerPositionChanged: (pos: Timestamp) => void,
    private onUnhandledClick: (pos: Timestamp) => void,
  ) {
    const ctx = canvas.getContext('2d');

    if (ctx === null) {
      throw Error('MiniTimeline canvas context was null!');
    }

    this.ctx = ctx;

    const onUnhandledClickInternal = async (mousePoint: Point) => {
      this.onUnhandledClick(
        this.getInput().transformer.untransform(mousePoint.x),
      );
    };
    this.handler = new CanvasMouseHandlerImpl(
      this,
      'pointer',
      onUnhandledClickInternal,
    );

    this.activePointer = new DraggableCanvasObjectImpl(
      this,
      () => this.getSelectedPosition(),
      (ctx: CanvasRenderingContext2D, position: number) => {
        const barWidth = 3;
        const triangleHeight = this.getPointerWidth() / 2;

        ctx.beginPath();
        ctx.moveTo(position - triangleHeight, 0);
        ctx.lineTo(position + triangleHeight, 0);
        ctx.lineTo(position + barWidth / 2, triangleHeight);
        ctx.lineTo(position + barWidth / 2, this.getHeight());
        ctx.lineTo(position - barWidth / 2, this.getHeight());
        ctx.lineTo(position - barWidth / 2, triangleHeight);
        ctx.closePath();
      },
      {
        fillStyle: Color.ACTIVE_POINTER,
        fill: true,
      },
      (x) => {
        const input = this.getInput();
        input.selectedPosition = x;
        this.onPointerPositionDragging(input.transformer.untransform(x));
      },
      (x) => {
        const input = this.getInput();
        input.selectedPosition = x;
        this.onPointerPositionChanged(input.transformer.untransform(x));
      },
      () => this.getUsableRange(),
    );
  }

  getSelectedPosition() {
    return this.getInput().selectedPosition;
  }

  async getTimelineEntries(): Promise<TimelineEntries> {
    return await this.getInput().getTimelineEntries();
  }

  getPadding(): Padding {
    const height = this.getHeight();
    const pointerWidth = this.getPointerWidth();
    return {
      top: Math.ceil(height / 10),
      bottom: Math.ceil(height / 10),
      left: Math.ceil(pointerWidth / 2),
      right: Math.ceil(pointerWidth / 2),
    };
  }

  getInnerHeight() {
    const padding = this.getPadding();
    return this.getHeight() - padding.top - padding.bottom;
  }

  async draw() {
    this.ctx.clearRect(0, 0, this.getWidth(), this.getHeight());
    await this.drawTraceLines();
    this.activePointer.draw(this.ctx);
  }

  private getPointerWidth() {
    return this.getHeight() / 6;
  }

  private async drawTraceLines() {
    const timelineEntries = await this.getTimelineEntries();
    const innerHeight = this.getInnerHeight();
    const lineHeight =
      innerHeight / (Math.max(timelineEntries.size - 10, 0) + 12);

    let fromTop = this.getPadding().top + innerHeight - lineHeight;

    timelineEntries.forEach((entries, traceType) => {
      this.ctx.globalAlpha = 0.7;
      this.ctx.fillStyle = TRACE_INFO[traceType].color;
      this.ctx.strokeStyle = 'blue';

      for (const entry of entries.points) {
        const width = 5;
        this.ctx.fillRect(entry - width / 2, fromTop, width, lineHeight);
      }

      for (const entry of entries.segments) {
        const width = Math.max(entry.to - entry.from, 3);
        this.ctx.fillRect(entry.from, fromTop, width, lineHeight);
      }

      this.ctx.fillStyle = Color.ACTIVE_POINTER;
      if (entries.activePoint) {
        const entry = entries.activePoint;
        const width = 5;
        this.ctx.fillRect(entry - width / 2, fromTop, width, lineHeight);
      }

      if (entries.activeSegment) {
        const entry = entries.activeSegment;
        const width = Math.max(entry.to - entry.from, 3);
        this.ctx.fillRect(entry.from, fromTop, width, lineHeight);
      }

      this.ctx.globalAlpha = 1.0;

      fromTop -= (lineHeight * 4) / 3;
    });
  }
}
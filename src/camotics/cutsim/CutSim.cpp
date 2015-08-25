/******************************************************************************\

    CAMotics is an Open-Source CAM software.
    Copyright (C) 2011-2015 Joseph Coffland <joseph@cauldrondevelopment.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

\******************************************************************************/

#include "CutSim.h"

#include "Workpiece.h"
#include "ToolPath.h"
#include "CutWorkpiece.h"
#include "Sweep.h"
#include "Project.h"

#include <tplang/TPLContext.h>
#include <tplang/Interpreter.h>

#include <camotics/contour/Surface.h>
#include <camotics/gcode/Interpreter.h>
#include <camotics/render/Renderer.h>
#include <camotics/sim/Controller.h>

#include <cbang/js/Javascript.h>
#include <cbang/os/SystemInfo.h>
#include <cbang/os/SystemUtilities.h>
#include <cbang/util/DefaultCatch.h>
#include <cbang/time/TimeInterval.h>

#include <limits>

using namespace std;
using namespace cb;
using namespace CAMotics;


CutSim::CutSim(Options &options) :
  Machine(options), threads(SystemInfo::instance().getCPUCount()) {
  options.pushCategory("Simulation");
  options.addTarget("threads", threads, "Number of simulation threads.");
  options.popCategory();
}


CutSim::~CutSim() {}


SmartPointer<ToolPath>
CutSim::computeToolPath(const SmartPointer<ToolTable> &tools,
                        const vector<string> &files) {
  // Setup
  Task::begin();
  Machine::reset();
  Controller controller(*this, tools);
  path = new ToolPath(tools);

  // Interpret code
  try {
    for (unsigned i = 0; i < files.size() && !Task::shouldQuit(); i++) {
      if (!SystemUtilities::exists(files[i])) continue;

      Task::update(0, "Running " + files[i]);

      if (String::endsWith(files[i], ".tpl")) {
        tplang::TPLContext ctx(cout, *this, controller.getToolTable());
        ctx.pushPath(files[i]);
        tplang::Interpreter(ctx).read(files[i]);

      } else // Assume GCode
        Interpreter(controller, SmartPointer<Task>::Phony(this)).read(files[i]);
    }
  } CATCH_ERROR;

  Task::end();
  return path.adopt();
}


SmartPointer<ToolPath> CutSim::computeToolPath(const Project &project) {
  SmartPointer<ToolTable> tools = project.getToolTable();

  vector<string> files;
  Project::iterator it;
  for (it = project.begin(); it != project.end(); it++)
    files.push_back((*it)->getAbsolutePath());

  return computeToolPath(tools, files);
}


SmartPointer<Surface> CutSim::computeSurface(const Simulation &sim) {
  // Setup cut simulation
  CutWorkpiece cutWP(new ToolSweep(sim.path, sim.time), sim.workpiece);

  // Render
  Renderer renderer(SmartPointer<Task>::Phony(this));
  return renderer.render(cutWP, threads, sim.resolution);
}



void CutSim::reduceSurface(Surface &surface) {
  LOG_INFO(1, "Reducing");

  double startCount = surface.getCount();

  Task::begin();
  Task::update(0, "Reducing...");

  surface.reduce(*this);

  unsigned count = surface.getCount();
  double r = (double)(startCount - count) / startCount * 100;

  double delta = Task::end();

  LOG_INFO(1, "Time: " << TimeInterval(delta)
           << String::printf(" Triangles: %u Reduction: %0.2f%%", count, r));
}


void CutSim::interrupt() {
  js::Javascript::terminate(); // End TPL code
  Task::interrupt();
}


void CutSim::move(const Move &move) {
  Machine::move(move);
  path->add(move);
}
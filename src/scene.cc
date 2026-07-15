#include "scene.h"

#include <clipper2/clipper.h>

namespace words {

void Scene::update(double t) {
  shape_.setAngle(t * 0.6);
  Clipper2Lib::PathsD world = shape_.worldPath();
  for (Entry& e : entries_) {
    e.hit = e.word.boxIntersects(world);
  }
}

}  // namespace words

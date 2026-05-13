#include <tesseract_common/macros.h>
TESSERACT_COMMON_IGNORE_WARNINGS_PUSH
#include <gtest/gtest.h>
#include <QApplication>
#include <QCoreApplication>
TESSERACT_COMMON_IGNORE_WARNINGS_POP

#include <tesseract_qt/common/component_info_manager.h>
#include <tesseract_qt/common/environment_manager.h>
#include <tesseract_qt/common/events/scene_graph_events.h>
#include <tesseract_qt/common/events/scene_graph_render_manager.h>

namespace
{
QApplication& getApplication()
{
  auto* existing_app = qobject_cast<QApplication*>(QCoreApplication::instance());
  if (existing_app != nullptr)
    return *existing_app;

  static int argc = 0;
  static QApplication* app = new QApplication(argc, nullptr);
  return *app;
}

class TestSceneGraphRenderManager : public tesseract_gui::SceneGraphRenderManager
{
public:
  using tesseract_gui::SceneGraphRenderManager::SceneGraphRenderManager;
  using tesseract_gui::SceneGraphRenderManager::takeEvents;

protected:
  void render() override {}
};
}  // namespace

TEST(SceneGraphRenderManagerUnit, TakeEventsDrainsQueuedEvents)
{
  auto& app = getApplication();
  (void)app;

  tesseract_gui::EnvironmentManager::clear();
  tesseract_gui::ComponentInfoManager::clear();

  auto component_info = tesseract_gui::ComponentInfoManager::create("render_queue_scene", "render_queue_component");
  ASSERT_NE(component_info, nullptr);

  TestSceneGraphRenderManager manager(component_info);

  tesseract_gui::events::SceneGraphClear first_event(component_info);
  QApplication::sendEvent(qApp, &first_event);

  auto first_batch = manager.takeEvents();
  ASSERT_EQ(first_batch.size(), 1);
  EXPECT_EQ(first_batch.front()->type(), tesseract_gui::events::EventType::SCENE_GRAPH_CLEAR);
  EXPECT_TRUE(manager.takeEvents().empty());

  tesseract_gui::events::SceneGraphClear second_event(component_info);
  QApplication::sendEvent(qApp, &second_event);

  auto second_batch = manager.takeEvents();
  ASSERT_EQ(second_batch.size(), 1);
  EXPECT_EQ(second_batch.front()->type(), tesseract_gui::events::EventType::SCENE_GRAPH_CLEAR);

  tesseract_gui::EnvironmentManager::clear();
  tesseract_gui::ComponentInfoManager::clear();
}
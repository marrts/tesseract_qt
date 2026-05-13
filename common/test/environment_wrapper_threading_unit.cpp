#include <tesseract_common/macros.h>
TESSERACT_COMMON_IGNORE_WARNINGS_PUSH
#include <gtest/gtest.h>
#include <QApplication>
#include <QCoreApplication>
#include <QEvent>
#include <QThread>
#include <chrono>
#include <future>
#include <thread>
TESSERACT_COMMON_IGNORE_WARNINGS_POP

#include <tesseract_environment/environment.h>
#include <tesseract_environment/commands/add_link_command.h>
#include <tesseract_qt/common/component_info_manager.h>
#include <tesseract_qt/common/environment_manager.h>
#include <tesseract_qt/common/environment_wrapper.h>
#include <tesseract_qt/common/events/scene_graph_events.h>
#include <tesseract_scene_graph/graph.h>
#include <tesseract_scene_graph/joint.h>
#include <tesseract_scene_graph/link.h>

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

tesseract_scene_graph::SceneGraph makeSceneGraph()
{
  tesseract_scene_graph::SceneGraph scene_graph("test_scene_graph");
  scene_graph.addLink(tesseract_scene_graph::Link("base_link"));
  return scene_graph;
}

tesseract_environment::AddLinkCommand makeAddLinkCommand()
{
  tesseract_scene_graph::Link link("tool_link");

  tesseract_scene_graph::Joint joint("tool_joint");
  joint.type = tesseract_scene_graph::JointType::FIXED;
  joint.parent_link_name = "base_link";
  joint.child_link_name = "tool_link";
  joint.parent_to_joint_origin_transform = Eigen::Isometry3d::Identity();

  return tesseract_environment::AddLinkCommand(link, joint);
}

class ThreadCaptureFilter : public QObject
{
public:
  void arm() { armed_ = true; }

  std::future<Qt::HANDLE> getFuture() { return promise_.get_future(); }

protected:
  bool eventFilter(QObject* watched, QEvent* event) override
  {
    if (armed_ && event->type() == tesseract_gui::events::EventType::SCENE_GRAPH_SET && !captured_)
    {
      captured_ = true;
      promise_.set_value(QThread::currentThreadId());
    }

    return QObject::eventFilter(watched, event);
  }

private:
  bool armed_{ false };
  bool captured_{ false };
  std::promise<Qt::HANDLE> promise_;
};
}  // namespace

TEST(EnvironmentWrapperThreadingUnit, BroadcastsEnvironmentEventsOnGuiThread)
{
  auto& app = getApplication();
  (void)app;

  tesseract_gui::EnvironmentManager::clear();
  tesseract_gui::ComponentInfoManager::clear();

  auto component_info = tesseract_gui::ComponentInfoManager::create("thread_test_scene", "thread_test_component");
  ASSERT_NE(component_info, nullptr);

  auto env = std::make_shared<tesseract_environment::Environment>();
  ASSERT_TRUE(env->init(makeSceneGraph()));

  auto wrapper = std::make_shared<tesseract_gui::DefaultEnvironmentWrapper>(component_info, env);
  tesseract_gui::EnvironmentManager::set(wrapper);

  ThreadCaptureFilter filter;
  qApp->installEventFilter(&filter);
  filter.arm();

  std::thread worker([env]() {
    env->applyCommand(std::make_shared<tesseract_environment::AddLinkCommand>(makeAddLinkCommand()));
  });

  auto future = filter.getFuture();
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (future.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready &&
         std::chrono::steady_clock::now() < deadline)
  {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  worker.join();

  ASSERT_EQ(future.wait_for(std::chrono::milliseconds(0)), std::future_status::ready);
  EXPECT_EQ(future.get(), qApp->thread()->currentThreadId());

  qApp->removeEventFilter(&filter);
  tesseract_gui::EnvironmentManager::clear();
  tesseract_gui::ComponentInfoManager::clear();
}
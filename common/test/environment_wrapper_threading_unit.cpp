#include <tesseract_common/macros.h>
TESSERACT_COMMON_IGNORE_WARNINGS_PUSH
#include <gtest/gtest.h>
#include <QApplication>
#include <QCoreApplication>
#include <QEvent>
#include <QThread>
#include <algorithm>
#include <chrono>
#include <future>
#include <thread>
TESSERACT_COMMON_IGNORE_WARNINGS_POP

#include <tesseract_environment/environment.h>
#include <tesseract_environment/commands/add_link_command.h>
#include <tesseract_environment/commands/remove_link_command.h>
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
    if (armed_ && event->type() == tesseract_gui::events::EventType::SCENE_GRAPH_ADD_LINK && !captured_)
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

class SceneGraphEventRecorder : public QObject
{
public:
  void reset()
  {
    scene_graph_set_count = 0;
    added_links.clear();
    added_joints.clear();
    removed_links.clear();
    removed_joints.clear();
  }

  int scene_graph_set_count{ 0 };
  std::vector<std::string> added_links;
  std::vector<std::string> added_joints;
  std::vector<std::string> removed_links;
  std::vector<std::string> removed_joints;

protected:
  bool eventFilter(QObject* watched, QEvent* event) override
  {
    using tesseract_gui::events::EventType;
    if (event->type() == EventType::SCENE_GRAPH_SET)
      ++scene_graph_set_count;
    else if (event->type() == EventType::SCENE_GRAPH_ADD_LINK)
      added_links.push_back(static_cast<tesseract_gui::events::SceneGraphAddLink*>(event)->getLink()->getName());
    else if (event->type() == EventType::SCENE_GRAPH_ADD_JOINT)
      added_joints.push_back(static_cast<tesseract_gui::events::SceneGraphAddJoint*>(event)->getJoint()->getName());
    else if (event->type() == EventType::SCENE_GRAPH_REMOVE_LINK)
      removed_links.push_back(static_cast<tesseract_gui::events::SceneGraphRemoveLink*>(event)->getLinkName());
    else if (event->type() == EventType::SCENE_GRAPH_REMOVE_JOINT)
      removed_joints.push_back(static_cast<tesseract_gui::events::SceneGraphRemoveJoint*>(event)->getJointName());

    return QObject::eventFilter(watched, event);
  }
};

void processPendingEvents()
{
  for (int i = 0; i < 5; ++i)
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

std::vector<std::string> sorted(std::vector<std::string> values)
{
  std::sort(values.begin(), values.end());
  return values;
}
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
TEST(EnvironmentWrapperThreadingUnit, AddRemoveLinkSendsIncrementalSceneGraphEvents)
{
  auto& app = getApplication();
  (void)app;

  tesseract_gui::EnvironmentManager::clear();
  tesseract_gui::ComponentInfoManager::clear();

  auto component_info = tesseract_gui::ComponentInfoManager::create("incremental_scene", "incremental_component");
  ASSERT_NE(component_info, nullptr);

  auto env = std::make_shared<tesseract_environment::Environment>();
  ASSERT_TRUE(env->init(makeSceneGraph()));

  auto wrapper = std::make_shared<tesseract_gui::DefaultEnvironmentWrapper>(component_info, env);
  tesseract_gui::EnvironmentManager::set(wrapper);
  processPendingEvents();

  SceneGraphEventRecorder recorder;
  qApp->installEventFilter(&recorder);

  // Add a link
  ASSERT_TRUE(env->applyCommand(std::make_shared<tesseract_environment::AddLinkCommand>(makeAddLinkCommand())));
  processPendingEvents();
  EXPECT_EQ(recorder.scene_graph_set_count, 0);
  EXPECT_EQ(recorder.added_links, std::vector<std::string>{ "tool_link" });
  EXPECT_EQ(recorder.added_joints, std::vector<std::string>{ "tool_joint" });
  EXPECT_TRUE(recorder.removed_links.empty());
  EXPECT_TRUE(recorder.removed_joints.empty());

  // Add a child of the new link
  recorder.reset();
  tesseract_scene_graph::Link tip_link("tip_link");
  tesseract_scene_graph::Joint tip_joint("tip_joint");
  tip_joint.type = tesseract_scene_graph::JointType::FIXED;
  tip_joint.parent_link_name = "tool_link";
  tip_joint.child_link_name = "tip_link";
  ASSERT_TRUE(env->applyCommand(std::make_shared<tesseract_environment::AddLinkCommand>(tip_link, tip_joint)));
  processPendingEvents();
  EXPECT_EQ(recorder.scene_graph_set_count, 0);
  EXPECT_EQ(recorder.added_links, std::vector<std::string>{ "tip_link" });

  // Replace an existing link: old visuals must be removed before the new ones are added
  recorder.reset();
  tesseract_scene_graph::Link replacement("tip_link");
  ASSERT_TRUE(
      env->applyCommand(std::make_shared<tesseract_environment::AddLinkCommand>(replacement, tip_joint, true)));
  processPendingEvents();
  EXPECT_EQ(recorder.scene_graph_set_count, 0);
  EXPECT_EQ(recorder.removed_links, std::vector<std::string>{ "tip_link" });
  EXPECT_EQ(recorder.added_links, std::vector<std::string>{ "tip_link" });

  // Removing a link also removes its children
  recorder.reset();
  ASSERT_TRUE(env->applyCommand(std::make_shared<tesseract_environment::RemoveLinkCommand>("tool_link")));
  processPendingEvents();
  EXPECT_EQ(recorder.scene_graph_set_count, 0);
  EXPECT_EQ(sorted(recorder.removed_links), (std::vector<std::string>{ "tip_link", "tool_link" }));
  EXPECT_EQ(sorted(recorder.removed_joints), (std::vector<std::string>{ "tip_joint", "tool_joint" }));
  EXPECT_TRUE(recorder.added_links.empty());

  qApp->removeEventFilter(&recorder);
  tesseract_gui::EnvironmentManager::clear();
  tesseract_gui::ComponentInfoManager::clear();
}

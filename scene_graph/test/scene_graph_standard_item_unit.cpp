#include <tesseract_common/macros.h>
TESSERACT_COMMON_IGNORE_WARNINGS_PUSH
#include <gtest/gtest.h>
#include <QApplication>
#include <QCoreApplication>
TESSERACT_COMMON_IGNORE_WARNINGS_POP

#include <tesseract_qt/common/component_info_manager.h>
#include <tesseract_qt/common/events/scene_graph_events.h>
#include <tesseract_qt/scene_graph/models/scene_graph_model.h>
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

tesseract_scene_graph::SceneGraph makeSceneGraph(
  const std::string& first_child_link,
  const std::string& joint_name)
{
  tesseract_scene_graph::SceneGraph scene_graph("scene_graph_under_test");
  scene_graph.addLink(tesseract_scene_graph::Link("base_link"));
  scene_graph.addLink(tesseract_scene_graph::Link(first_child_link));

  tesseract_scene_graph::Joint joint(joint_name);
  joint.type = tesseract_scene_graph::JointType::FIXED;
  joint.parent_link_name = "base_link";
  joint.child_link_name = first_child_link;
  scene_graph.addJoint(joint);

  return scene_graph;
}

std::shared_ptr<const tesseract_scene_graph::Joint> makeMovedJoint(
  const std::string& joint_name,
  const std::string& child_link_name)
{
  auto joint = std::make_shared<tesseract_scene_graph::Joint>(joint_name);
  joint->type = tesseract_scene_graph::JointType::FIXED;
  joint->parent_link_name = "base_link";
  joint->child_link_name = child_link_name;
  return joint;
}
}  // namespace

TEST(SceneGraphStandardItemUnit, SetSceneGraphRemovesAllExistingLinkAndJointRows)
{
  auto& app = getApplication();
  (void)app;

  tesseract_gui::ComponentInfoManager::clear();
  auto component_info = tesseract_gui::ComponentInfoManager::create("scene_graph_test_scene", "scene_graph_test");
  ASSERT_NE(component_info, nullptr);

  tesseract_gui::SceneGraphModel model(component_info);

  auto first_scene_graph = std::make_shared<tesseract_scene_graph::SceneGraph>(makeSceneGraph("first_link", "first_joint"));
  tesseract_gui::events::SceneGraphSet first_event(component_info, first_scene_graph);
  QApplication::sendEvent(qApp, &first_event);

  auto second_scene_graph =
    std::make_shared<tesseract_scene_graph::SceneGraph>(makeSceneGraph("second_link", "second_joint"));
  tesseract_gui::events::SceneGraphSet second_event(component_info, second_scene_graph);
  QApplication::sendEvent(qApp, &second_event);

  auto* root_item = model.item(0, 0);
  ASSERT_NE(root_item, nullptr);

  auto* links_item = root_item->child(1, 0);
  auto* joints_item = root_item->child(2, 0);
  ASSERT_NE(links_item, nullptr);
  ASSERT_NE(joints_item, nullptr);

  EXPECT_EQ(links_item->rowCount(), 2);
  EXPECT_EQ(joints_item->rowCount(), 1);

  auto* first_link_item = links_item->child(0, 0);
  auto* second_link_item = links_item->child(1, 0);
  ASSERT_NE(first_link_item, nullptr);
  ASSERT_NE(second_link_item, nullptr);
  EXPECT_EQ(first_link_item->text().toStdString(), "base_link");
  EXPECT_EQ(second_link_item->text().toStdString(), "second_link");

  tesseract_gui::ComponentInfoManager::clear();
}

TEST(SceneGraphStandardItemUnit, MoveLinkReplacesExistingJointForChildLink)
{
  auto& app = getApplication();
  (void)app;

  tesseract_gui::ComponentInfoManager::clear();
  auto component_info = tesseract_gui::ComponentInfoManager::create("scene_graph_move_scene", "scene_graph_move");
  ASSERT_NE(component_info, nullptr);

  tesseract_gui::SceneGraphModel model(component_info);

  auto scene_graph = std::make_shared<tesseract_scene_graph::SceneGraph>(makeSceneGraph("first_link", "first_joint"));
  tesseract_gui::events::SceneGraphSet set_event(component_info, scene_graph);
  QApplication::sendEvent(qApp, &set_event);

  tesseract_gui::events::SceneGraphMoveLink move_event(component_info, makeMovedJoint("second_joint", "first_link"));
  QApplication::sendEvent(qApp, &move_event);

  auto* root_item = model.item(0, 0);
  ASSERT_NE(root_item, nullptr);

  auto* joints_item = root_item->child(2, 0);
  ASSERT_NE(joints_item, nullptr);
  ASSERT_EQ(joints_item->rowCount(), 1);

  auto* joint_item = joints_item->child(0, 0);
  ASSERT_NE(joint_item, nullptr);
  EXPECT_EQ(joint_item->text().toStdString(), "second_joint");

  tesseract_gui::ComponentInfoManager::clear();
}
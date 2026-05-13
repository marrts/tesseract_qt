/**
 * @author Levi Armstrong <levi.armstrong@gmail.com>
 *
 * @copyright Copyright (C) 2022 Levi Armstrong <levi.armstrong@gmail.com>
 *
 * @par License
 * GNU Lesser General Public License Version 3, 29 June 2007
 * @par
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 * @par
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * @par
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <tesseract_qt/common/environment_wrapper.h>
#include <tesseract_qt/common/component_info.h>

#include <tesseract_qt/common/events/allowed_collision_matrix_events.h>
#include <tesseract_qt/common/events/contact_results_events.h>
#include <tesseract_qt/common/events/scene_graph_events.h>
#include <tesseract_qt/common/events/group_joint_states_events.h>
#include <tesseract_qt/common/events/group_tcps_events.h>
#include <tesseract_qt/common/events/kinematic_groups_events.h>
#include <tesseract_qt/common/events/environment_events.h>
#include <tesseract_qt/common/events/status_log_events.h>

#include <tesseract_collision/core/discrete_contact_manager.h>

#include <tesseract_scene_graph/link.h>
#include <tesseract_scene_graph/joint.h>
#include <tesseract_scene_graph/graph.h>
#include <tesseract_scene_graph/scene_state.h>

#include <tesseract_state_solver/state_solver.h>

#include <tesseract_environment/environment.h>
#include <tesseract_environment/environment_monitor.h>
#include <tesseract_environment/events.h>
#include <tesseract_environment/command.h>
#include <tesseract_environment/commands/add_contact_managers_plugin_info_command.h>

#include <tesseract_collision/core/common.h>
#include <tesseract_common/yaml_utils.h>
#include <tesseract_common/yaml_extensions.h>

#include <QApplication>
#include <QThread>

static const std::string DEFAULT_CONTACT_MANAGER_PLUGINS =
    R"(contact_manager_plugins:
         search_paths:
           - /usr/local/lib
         search_libraries:
           - tesseract_collision_bullet_factories
         discrete_plugins:
           default: BulletDiscreteBVHManager
           plugins:
             BulletDiscreteBVHManager:
               class: BulletDiscreteBVHManagerFactory)";

template <typename Factory>
void dispatchApplicationEvent(Factory&& factory)
{
  if (qApp == nullptr)
    return;

  auto event = factory();
  if (QThread::currentThread() == qApp->thread())
    QApplication::sendEvent(qApp, event.get());
  else
    QApplication::postEvent(qApp, event.release());
}

void applyDefaultContactManager(tesseract_environment::Environment& env)
{
  YAML::Node config;
  try
  {
    config = YAML::Load(DEFAULT_CONTACT_MANAGER_PLUGINS);
  }
  // LCOV_EXCL_START
  catch (...)
  {
    return;
  }
  // LCOV_EXCL_STOP

  const YAML::Node& cm_plugin_info = config[tesseract_common::ContactManagersPluginInfo::CONFIG_KEY];
  auto contact_managers_plugin_info = cm_plugin_info.as<tesseract_common::ContactManagersPluginInfo>();

  env.applyCommand(
      std::make_shared<tesseract_environment::AddContactManagersPluginInfoCommand>(contact_managers_plugin_info));
}

///////////////////////////////////////

void tesseractEventFilterHelper(const tesseract_environment::Event& event,
                                tesseract_gui::EnvironmentWrapper& env_wrapper,
                                int& current_revision)
{
  if (!env_wrapper.getEnvironment()->isInitialized())
    return;

  switch (event.type)
  {
    case tesseract_environment::Events::COMMAND_APPLIED:
    {
      const auto& e = static_cast<const tesseract_environment::CommandAppliedEvent&>(event);
      if (current_revision == 0 || e.revision < current_revision)
      {
        env_wrapper.broadcast();
      }
      else
      {
        /** @todo update to handle explicit commands */
        bool broadcast{ false };
        for (std::size_t i = current_revision; i < e.revision; ++i)
        {
          const auto& cmd = e.commands.at(i);
          switch (cmd->getType())
          {
            case tesseract_environment::CommandType::ADD_SCENE_GRAPH:
            case tesseract_environment::CommandType::ADD_LINK:
            case tesseract_environment::CommandType::ADD_TRAJECTORY_LINK:
            case tesseract_environment::CommandType::CHANGE_LINK_VISIBILITY:
            case tesseract_environment::CommandType::REMOVE_LINK:
            case tesseract_environment::CommandType::REMOVE_JOINT:
            {
              broadcast = true;
              break;
            }
            case tesseract_environment::CommandType::MOVE_LINK:
            case tesseract_environment::CommandType::MOVE_JOINT:
            case tesseract_environment::CommandType::REPLACE_JOINT:
            case tesseract_environment::CommandType::CHANGE_JOINT_ORIGIN:
            case tesseract_environment::CommandType::CHANGE_LINK_ORIGIN:
            case tesseract_environment::CommandType::CHANGE_LINK_COLLISION_ENABLED:
            case tesseract_environment::CommandType::MODIFY_ALLOWED_COLLISIONS:
            case tesseract_environment::CommandType::REMOVE_ALLOWED_COLLISION_LINK:
            case tesseract_environment::CommandType::CHANGE_JOINT_POSITION_LIMITS:
            case tesseract_environment::CommandType::CHANGE_JOINT_VELOCITY_LIMITS:
            case tesseract_environment::CommandType::CHANGE_JOINT_ACCELERATION_LIMITS:
            case tesseract_environment::CommandType::ADD_KINEMATICS_INFORMATION:
            case tesseract_environment::CommandType::CHANGE_COLLISION_MARGINS:
            case tesseract_environment::CommandType::ADD_CONTACT_MANAGERS_PLUGIN_INFO:
            case tesseract_environment::CommandType::SET_ACTIVE_CONTINUOUS_CONTACT_MANAGER:
            case tesseract_environment::CommandType::SET_ACTIVE_DISCRETE_CONTACT_MANAGER:
            {
              break;
            }
            // LCOV_EXCL_START
            default:
            {
              dispatchApplicationEvent([]() {
                return std::make_unique<tesseract_gui::events::StatusLogInfo>(
                    "Tesseract Qt Environment Wrapper, Unhandled environment command");
              });
            }
          }
        }
        if (broadcast)
          env_wrapper.broadcast();
      }
      current_revision = e.revision;
      break;
    }
    case tesseract_environment::Events::SCENE_STATE_CHANGED:
    {
      const auto& e = static_cast<const tesseract_environment::SceneStateChangedEvent&>(event);
      auto component_info = env_wrapper.getComponentInfo();
      auto state = e.state;
      dispatchApplicationEvent([component_info = std::move(component_info), state = std::move(state)]() {
        return std::make_unique<tesseract_gui::events::SceneStateChanged>(component_info, state);
      });
      break;
    }
  }
}

void eventFilterHelper(QObject* /*obj*/,
                       QEvent* event,
                       const std::shared_ptr<const tesseract_gui::ComponentInfo>& component_info,
                       tesseract_environment::Environment& env)
{
  if (event->type() == tesseract_gui::events::EventType::CONTACT_RESULTS_COMPUTE)
  {
    assert(dynamic_cast<tesseract_gui::events::ContactResultsCompute*>(event) != nullptr);
    auto* e = static_cast<tesseract_gui::events::ContactResultsCompute*>(event);
    if (e->getComponentInfo() != component_info)
      return;

    tesseract_collision::ContactResultMap contacts;
    switch (e->getStateType())
    {
      case tesseract_gui::events::ContactResultsCompute::StateType::CURRENT_STATE:
      {
        tesseract_collision::DiscreteContactManager::UPtr contact_manager = env.getDiscreteContactManager();
        if (contact_manager == nullptr)
        {
          applyDefaultContactManager(env);
          contact_manager = env.getDiscreteContactManager();
        }
        contact_manager->applyContactManagerConfig(e->getContactManagerConfig());
        contact_manager->contactTest(contacts, e->getCollisionCheckConfig().contact_request);
        break;
      }
      case tesseract_gui::events::ContactResultsCompute::StateType::NAMED_STATE:
      {
        break;
      }
      case tesseract_gui::events::ContactResultsCompute::StateType::USER_DEFINED_STATE:
      {
        break;
      }
    }

    // Convert to tracked objects
    tesseract_gui::ContactResultMap tracked_object;
    for (const auto& contact : contacts)
    {
      tesseract_gui::ContactResultVector crv;
      for (const auto& result : contact.second)
        crv().emplace_back(tesseract_gui::ContactResult(result));

      tracked_object[contact.first] = crv;
    }

    auto event_namespace = e->getNamespace();
    dispatchApplicationEvent([component_info = std::move(component_info),
                              tracked_object = std::move(tracked_object),
                              event_namespace = std::move(event_namespace)]() mutable {
      return std::make_unique<tesseract_gui::events::ContactResultsSet>(component_info, tracked_object, event_namespace);
    });
  }
  else if (event->type() == tesseract_gui::events::EventType::ACM_GENERATE)
  {
    assert(dynamic_cast<tesseract_gui::events::AllowedCollisionMatrixGenerate*>(event) != nullptr);
    auto* e = static_cast<tesseract_gui::events::AllowedCollisionMatrixGenerate*>(event);
    if (e->getComponentInfo() != component_info)
      return;

    tesseract_collision::DiscreteContactManager::UPtr contact_manager = env.getDiscreteContactManager();
    if (contact_manager == nullptr)
    {
      applyDefaultContactManager(env);
      contact_manager = env.getDiscreteContactManager();
    }

    auto state_solver = env.getStateSolver();

    // We want to disable the allowed contact function for this process so it is set null
    contact_manager->setContactAllowedValidator(nullptr);
    tesseract_collision::ContactResultMap results;
    tesseract_collision::ContactRequest request;
    request.type = tesseract_collision::ContactTestType::ALL;

    for (long i = 0; i < e->getResolution(); ++i)
    {
      tesseract_scene_graph::SceneState state = state_solver->getRandomState();
      contact_manager->setCollisionObjectsTransform(state.link_transforms);
      contact_manager->contactTest(results, request);
    }

    tesseract_common::AllowedCollisionMatrix acm;
    for (const auto& pair : results)
    {
      double percent = double(pair.second.size()) / double(e->getResolution());
      if (percent > 0.95)
      {
        std::vector<std::string> adj_first = env.getSceneGraph()->getAdjacentLinkNames(pair.first.first);
        std::vector<std::string> adj_second = env.getSceneGraph()->getAdjacentLinkNames(pair.first.second);
        if (std::find(adj_first.begin(), adj_first.end(), pair.first.second) != adj_first.end())
          acm.addAllowedCollision(pair.first.first, pair.first.second, "Adjacent");
        else if (std::find(adj_second.begin(), adj_second.end(), pair.first.first) != adj_second.end())
          acm.addAllowedCollision(pair.first.second, pair.first.first, "Adjacent");
        else
          acm.addAllowedCollision(pair.first.second, pair.first.first, "Allways");
      }
    }

    std::vector<std::string> link_names = env.getLinkNames();
    for (std::size_t i = 0; i < link_names.size() - 1; ++i)
    {
      const auto& link1 = env.getLink(link_names[i]);
      if (link1->collision.empty())
        continue;

      for (std::size_t j = i + 1; j < link_names.size(); ++j)
      {
        const auto& link2 = env.getLink(link_names[j]);
        if (link2->collision.empty())
          continue;

        if (results.find(tesseract_common::makeOrderedLinkPair(link_names[i], link_names[j])) == results.end())
          acm.addAllowedCollision(link_names[i], link_names[j], "Never");
      }
    }

    dispatchApplicationEvent([component_info = std::move(component_info), acm = std::move(acm)]() mutable {
      return std::make_unique<tesseract_gui::events::AllowedCollisionMatrixSet>(component_info, acm);
    });
  }
  else if (event->type() == tesseract_gui::events::EventType::ENVIRONMENT_APPLY_COMMANDS)
  {
    assert(dynamic_cast<tesseract_gui::events::EnvironmentApplyCommand*>(event) != nullptr);
    auto* e = static_cast<tesseract_gui::events::EnvironmentApplyCommand*>(event);
    if (e->getComponentInfo() != component_info)
      return;

    env.applyCommands(e->getCommands());
  }
}

void broadcastHelper(const std::shared_ptr<const tesseract_gui::ComponentInfo>& component_info,
                     const tesseract_environment::Environment& env)
{
  auto lock = env.lockRead();

  if (!env.isInitialized())
    return;

  // Broadcast environment data
  auto scene_graph = std::shared_ptr<const tesseract_scene_graph::SceneGraph>(env.getSceneGraph()->clone());
  dispatchApplicationEvent([component_info, scene_graph = std::move(scene_graph)]() mutable {
    return std::make_unique<tesseract_gui::events::SceneGraphSet>(component_info, scene_graph);
  });

  auto state = env.getState();
  dispatchApplicationEvent([component_info, state = std::move(state)]() mutable {
    return std::make_unique<tesseract_gui::events::SceneStateChanged>(component_info, state);
  });

  auto command_history = env.getCommandHistory();
  dispatchApplicationEvent([component_info, command_history = std::move(command_history)]() mutable {
    return std::make_unique<tesseract_gui::events::EnvironmentCommandsSet>(component_info, command_history);
  });

  auto acm = *env.getAllowedCollisionMatrix();
  dispatchApplicationEvent([component_info, acm = std::move(acm)]() mutable {
    return std::make_unique<tesseract_gui::events::AllowedCollisionMatrixSet>(component_info, acm);
  });

  auto kin_info = env.getKinematicsInformation();
  auto chain_groups = kin_info.chain_groups;
  auto joint_groups = kin_info.joint_groups;
  auto link_groups = kin_info.link_groups;
  dispatchApplicationEvent([component_info,
                            chain_groups = std::move(chain_groups),
                            joint_groups = std::move(joint_groups),
                            link_groups = std::move(link_groups)]() mutable {
    return std::make_unique<tesseract_gui::events::KinematicGroupsSet>(
        component_info, chain_groups, joint_groups, link_groups);
  });

  auto group_states = kin_info.group_states;
  dispatchApplicationEvent([component_info, group_states = std::move(group_states)]() mutable {
    return std::make_unique<tesseract_gui::events::GroupJointStatesSet>(component_info, group_states);
  });

  auto group_tcps = kin_info.group_tcps;
  dispatchApplicationEvent([component_info, group_tcps = std::move(group_tcps)]() mutable {
    return std::make_unique<tesseract_gui::events::GroupTCPsSet>(component_info, group_tcps);
  });
}

namespace tesseract_gui
{
EnvironmentWrapper::EnvironmentWrapper(std::shared_ptr<const ComponentInfo> component_info)
  : component_info_(std::move(component_info))
{
}

EnvironmentWrapper::~EnvironmentWrapper()
{
  // clear environment data
  auto component_info = component_info_;
  dispatchApplicationEvent([component_info]() { return std::make_unique<events::SceneGraphClear>(component_info); });

  dispatchApplicationEvent([component_info]() {
    return std::make_unique<events::EnvironmentCommandsClear>(component_info);
  });

  dispatchApplicationEvent([component_info]() {
    return std::make_unique<events::AllowedCollisionMatrixClear>(component_info);
  });

  dispatchApplicationEvent([component_info]() { return std::make_unique<events::KinematicGroupsClear>(component_info); });

  dispatchApplicationEvent([component_info]() {
    return std::make_unique<events::GroupJointStatesClear>(component_info);
  });

  dispatchApplicationEvent([component_info]() { return std::make_unique<events::GroupTCPsClear>(component_info); });
}

std::shared_ptr<const ComponentInfo> EnvironmentWrapper::getComponentInfo() const { return component_info_; }

void EnvironmentWrapper::broadcast() const { broadcastHelper(getComponentInfo(), *getEnvironment()); }

void EnvironmentWrapper::init()
{
  if (initialized_)
    throw std::runtime_error("EnvironmentWrapper::init was called multipel times");

  // Get current revision
  revision_ = environment().getRevision();

  // Add environment event callback
  std::size_t uuid = std::hash<EnvironmentWrapper*>()(this);
  environment().addEventCallback(
      uuid, [this](const tesseract_environment::Event& event) { tesseractEventFilterHelper(event, *this, revision_); });

  // Broadcast data to initialize available widgets
  broadcast();

  initialized_ = true;
}

DefaultEnvironmentWrapper::DefaultEnvironmentWrapper(std::shared_ptr<tesseract_environment::Environment> env)
  : DefaultEnvironmentWrapper(nullptr, std::move(env))
{
}

DefaultEnvironmentWrapper::DefaultEnvironmentWrapper(std::shared_ptr<const ComponentInfo> component_info,
                                                     std::shared_ptr<tesseract_environment::Environment> env)
  : EnvironmentWrapper(std::move(component_info)), env_(std::move(env))
{
  // Install event filter for interactive view controller
  qGuiApp->installEventFilter(this);
}

DefaultEnvironmentWrapper::~DefaultEnvironmentWrapper()
{
  if (initialized_ && env_)
    env_->removeEventCallback(std::hash<EnvironmentWrapper*>()(this));
}

std::shared_ptr<const tesseract_environment::Environment> DefaultEnvironmentWrapper::getEnvironment() const
{
  return env_;
}

std::shared_ptr<tesseract_environment::Environment> DefaultEnvironmentWrapper::getEnvironment() { return env_; }

const tesseract_environment::Environment& DefaultEnvironmentWrapper::environment() const { return *env_; }

tesseract_environment::Environment& DefaultEnvironmentWrapper::environment() { return *env_; }

bool DefaultEnvironmentWrapper::eventFilter(QObject* obj, QEvent* event)
{
  eventFilterHelper(obj, event, getComponentInfo(), *env_);

  // Standard event processing
  return QObject::eventFilter(obj, event);
}

MonitorEnvironmentWrapper::MonitorEnvironmentWrapper(
    std::shared_ptr<tesseract_environment::EnvironmentMonitor> env_monitor)
  : MonitorEnvironmentWrapper(nullptr, std::move(env_monitor))
{
}

MonitorEnvironmentWrapper::MonitorEnvironmentWrapper(
    std::shared_ptr<const ComponentInfo> component_info,
    std::shared_ptr<tesseract_environment::EnvironmentMonitor> env_monitor)
  : EnvironmentWrapper(std::move(component_info)), env_monitor_(std::move(env_monitor))
{
  // Install event filter for interactive view controller
  qGuiApp->installEventFilter(this);
}

MonitorEnvironmentWrapper::~MonitorEnvironmentWrapper()
{
  if (initialized_ && env_monitor_)
    environment().removeEventCallback(std::hash<EnvironmentWrapper*>()(this));
}

std::shared_ptr<const tesseract_environment::Environment> MonitorEnvironmentWrapper::getEnvironment() const
{
  return env_monitor_->getEnvironment();
}

std::shared_ptr<tesseract_environment::Environment> MonitorEnvironmentWrapper::getEnvironment()
{
  return env_monitor_->getEnvironment();
}

const tesseract_environment::Environment& MonitorEnvironmentWrapper::environment() const
{
  return env_monitor_->environment();
}

tesseract_environment::Environment& MonitorEnvironmentWrapper::environment() { return env_monitor_->environment(); }

std::shared_ptr<tesseract_environment::EnvironmentMonitor> MonitorEnvironmentWrapper::getEnvironmentMonitor()
{
  return env_monitor_;
}

std::shared_ptr<const tesseract_environment::EnvironmentMonitor>
MonitorEnvironmentWrapper::getEnvironmentMonitor() const
{
  return env_monitor_;
}

bool MonitorEnvironmentWrapper::eventFilter(QObject* obj, QEvent* event)
{
  eventFilterHelper(obj, event, getComponentInfo(), env_monitor_->environment());

  // Standard event processing
  return QObject::eventFilter(obj, event);
}
}  // namespace tesseract_gui

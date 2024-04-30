/// \file
/// \brief The multisim node is a simulation and visualization tool for the turtlebot3 robots.
///        It uses rviz2 for visualization and provides a simulated environment. The package
///        creates stationary walls and obstacles and track the position of a red robot.
///
/// PARAMETERS:
///     \param rate (int): Timer callback frequency [Hz]
///     \param x0 (double): Initial x coordinate of the robot [m]
///     \param y0 (double): Initial y coordinate of the robot [m]
///     \param theta0 (double): Initial theta angle of the robot [radians]
///     \param obstacles.x (std::vector<double>): Vector of x coordinates for each obstacle [m]
///     \param obstacles.y (std::vector<double>): Vector of y coordinates for each obstacle [m]
///     \param obstacles.r (double): Radius of cylindrical obstacles [m]
///     \param arena_x_length (double): Inner length of arena in x direction [m]
///     \param arena_y_length (double): Inner length of arena in y direction [m]
///
/// PUBLISHES:
///     \param ~/timestep (std_msgs::msg::UInt64): Current simulation timestep
///     \param ~/obstacles (visualization_msgs::msg::MarkerArray): Marker obstacles that are
///                                                                displayed in Rviz
///     \param ~/walls (visualization_msgs::msg::MarkerArray): Marker walls that are
///                                                            displayed in Rviz
///
/// SUBSCRIBES:
///     None
///
/// SERVERS:
///     \param ~/reset (std_srvs::srv::Empty): Resets simulation to initial state
///     \param ~/teleport (multisim::srv::Teleport): Teleport robot to a specific pose
///
/// CLIENTS:
///     None
///
/// BROADCASTERS:
///     \param tf_broadcaster_ (tf2_ros::TransformBroadcaster): Broadcasts red turtle position

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <random>
#include <queue>

#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/string.hpp"
#include "std_msgs/msg/u_int64.hpp"
#include "std_srvs/srv/empty.hpp"
#include "geometry_msgs/msg/transform_stamped.hpp"
#include "tf2/LinearMath/Quaternion.h"
#include "tf2_ros/transform_broadcaster.h"
#include "nav_msgs/msg/path.hpp"
#include "multisim/srv/teleport.hpp"
#include "nuturtlebot_msgs/msg/wheel_commands.hpp"
#include "nuturtlebot_msgs/msg/sensor_data.hpp"
#include "visualization_msgs/msg/marker_array.hpp"
#include "visualization_msgs/msg/marker.hpp"
#include "turtlelib/geometry2d.hpp"
#include "turtlelib/se2d.hpp"
#include "turtlelib/diff_drive.hpp"
#include "sensor_msgs/msg/laser_scan.hpp"

using namespace std::chrono_literals;

/// \brief This class publishes the current timestep of the simulation, obstacles and walls that
///        appear in Rviz as markers. The class has a timer_callback to continually update the
///        simulation at each timestep. The reset service resets the simulation to the initial
///        state thus restarting the simulation. A teleport service is available to teleport a
///        turtlebot to any pose. A broadcaster broadcasts the robots TF frames to a topic for
///        visualization in Rviz. The simulation operates in a loop, updating the state of the
///        world, publishing messages that provides state information simulating a real robot,
///        and processing service/subscriber callbacks for commands for the next time step. The
///        loop runs at a fixed frequency until termination.
///
///  \param rate (int): Timer callback frequency [Hz]
///  \param x0_ (double): Initial x coordinate of the robot [m]
///  \param y0_ (double): Initial y coordinate of the robot [m]
///  \param theta0_ (double): Initial theta angle of the robot [radians]
///  \param theta_ (double): Current theta angle of the robot [radians]
///  \param obstacles_x_ (std::vector<double>): Vector of x coordinates for each obstacle [m]
///  \param obstacles_y_ (std::vector<double>): Vector of y coordinates for each obstacle [m]
///  \param obstacles_r_ (double): Radius of cylindrical obstacles [m]
///  \param arena_x_length_ (double): Inner length of arena in x direction [m]
///  \param arena_y_length_ (double): Inner length of arena in y direction [m]
///  \param wheel_radius_ (double): Inner length of arena in y direction [m]
///  \param track_width_ (double): Inner length of arena in y direction [m]
///  \param encoder_ticks_per_rad_ (double): Inner length of arena in y direction [m]
///  \param motor_cmd_per_rad_sec (double): Inner length of arena in y direction [m]
///  \param input_noise_ (double): Inner length of arena in y direction [m]
///  \param slip_fraction_ (double): Inner length of arena in y direction [m]
///  \param max_range_ (double): Inner length of arena in y direction [m]
///  \param collision_radius_ (double): Inner length of arena in y direction [m]
///  \param lidar_variance_ (double): Inner length of arena in y direction [m]
///  \param lidar_min_range_ (double): Inner length of arena in y direction [m]
///  \param lidar_max_range_ (double): Inner length of arena in y direction [m]
///  \param lidar_angle_increment_ (double): Inner length of arena in y direction [m]
///  \param lidar_num_samples_ (double): Inner length of arena in y direction [m]
///  \param lidar_resolution_des_ (double): Inner length of arena in y direction [m]

/// \brief Generate random number
std::mt19937 & get_random()
{
  // static variables inside a function are created once and persist for the remainder of the program
  static std::random_device rd{};
  static std::mt19937 mt{rd()};
  // we return a reference to the pseudo-random number genrator object. This is always the
  // same object every time get_random is called
  return mt;
}

class Multisim : public rclcpp::Node
{
public:
  Multisim()
  : Node("multisim"), timestep_(0)
  {
    // Parameter description
    auto seed_des = rcl_interfaces::msg::ParameterDescriptor{};
    auto num_robots_des = rcl_interfaces::msg::ParameterDescriptor{};
    auto rate_des = rcl_interfaces::msg::ParameterDescriptor{};
    auto arena_x_min_des = rcl_interfaces::msg::ParameterDescriptor{};
    auto arena_x_max_des = rcl_interfaces::msg::ParameterDescriptor{};
    auto arena_y_min_des = rcl_interfaces::msg::ParameterDescriptor{};
    auto arena_y_max_des = rcl_interfaces::msg::ParameterDescriptor{};
    auto min_corridor_width_des = rcl_interfaces::msg::ParameterDescriptor{};
    auto wall_breadth_des = rcl_interfaces::msg::ParameterDescriptor{};
    auto wall_length_des = rcl_interfaces::msg::ParameterDescriptor{};
    auto wall_num_des = rcl_interfaces::msg::ParameterDescriptor{};
    auto wheel_radius_des = rcl_interfaces::msg::ParameterDescriptor{};
    auto track_width_des = rcl_interfaces::msg::ParameterDescriptor{};
    auto encoder_ticks_per_rad_des = rcl_interfaces::msg::ParameterDescriptor{};
    auto motor_cmd_per_rad_sec_des = rcl_interfaces::msg::ParameterDescriptor{};
    auto input_noise_des = rcl_interfaces::msg::ParameterDescriptor{};
    auto slip_fraction_des = rcl_interfaces::msg::ParameterDescriptor{};
    auto collision_radius_des = rcl_interfaces::msg::ParameterDescriptor{};
    auto lidar_variance_des = rcl_interfaces::msg::ParameterDescriptor{};
    auto lidar_min_range_des = rcl_interfaces::msg::ParameterDescriptor{};
    auto lidar_max_range_des = rcl_interfaces::msg::ParameterDescriptor{};
    auto lidar_angle_increment_des = rcl_interfaces::msg::ParameterDescriptor{};
    auto lidar_num_samples_des = rcl_interfaces::msg::ParameterDescriptor{};
    auto lidar_resolution_des = rcl_interfaces::msg::ParameterDescriptor{};

    seed_des.description = "random seed value to configure the environment. Integer from [1, max_seed]";
    num_robots_des.description = "number of agents";
    rate_des.description = "Timer callback frequency [Hz]";
    arena_x_min_des.description = "Minimum length of arena along x [m]";
    arena_x_max_des.description = "Maxmimum length of arena along x [m]";
    arena_y_min_des.description = "Minimum length of arena along y [m]";
    arena_y_max_des.description = "Maximum length of arena along y [m]";
    min_corridor_width_des.description = "Minimum width of any corrdior [m]";
    wall_breadth_des.description = "Breadth of a wall [m]";
    wall_length_des.description = "Length of a wall [m]";
    wall_num_des.description = "Number of walls";
    wheel_radius_des.description = "Radius of the wheels [m]";
    track_width_des.description = "Separation between the wheels [m]";
    encoder_ticks_per_rad_des.description = "The number of encoder 'ticks' per radian [ticks/rad]";
    motor_cmd_per_rad_sec_des.description = "Radius per second in every motor command unit [(rad/s) / mcu]. Stupid name, I know!";
    input_noise_des.description = "Variance of noise due to non-ideal motor behaviour [(rad/s)^2]";
    slip_fraction_des.description = "Fractional range in which wheel can slip [-slip_fraction, +slip_fraction] [dimensionless]";
    collision_radius_des.description = "Collision radius of the robot [m]";
    lidar_variance_des.description = "Variance in LIDAR scanning [m]";
    lidar_min_range_des.description = "Max range of LIDAR scanning [m]";
    lidar_max_range_des.description = "Min range of LIDAR scanning [m]";
    lidar_angle_increment_des.description = "Angular increment LIDAR scanning [deg]";
    lidar_num_samples_des.description = "";
    lidar_resolution_des.description = "Distance resolution in LIDAR scanning [m]";

    // Declare default parameters values
    declare_parameter("seed", 0, seed_des);     // 1,2,3 ... max_seed_
    declare_parameter("num_robots", 0, num_robots_des);     // 1,2,3,..
    declare_parameter("rate", 200, rate_des);     // Hz for timer_callback
    declare_parameter("arena_x_min", 0.0, arena_x_min_des);  // Meters
    declare_parameter("arena_x_max", 0.0, arena_x_max_des);  // Meters
    declare_parameter("arena_y_min", 0.0, arena_y_min_des);  // Meters
    declare_parameter("arena_y_max", 0.0, arena_y_max_des);  // Meters
    declare_parameter("min_corridor_width", 0.0, min_corridor_width_des);  // Meters
    declare_parameter("wall_breadth", 0.0, wall_breadth_des);  // Meters
    declare_parameter("wall_length", 0.0, wall_length_des);  // Meters
    declare_parameter("wall_num", 0, wall_num_des); 
    declare_parameter("wheel_radius", -1.0, wheel_radius_des); // Meters
    declare_parameter("track_width", -1.0, track_width_des);  // Meters
    declare_parameter("encoder_ticks_per_rad", -1.0, encoder_ticks_per_rad_des); // Ticks per radian
    declare_parameter("motor_cmd_per_rad_sec", -1.0, motor_cmd_per_rad_sec_des); // MCU per radian/s
    declare_parameter("input_noise", -1.0, input_noise_des); // (radian/s)^2
    declare_parameter("slip_fraction", -1.0, slip_fraction_des); // Dimensionless (radian/radian)
    declare_parameter("collision_radius", -1.0, collision_radius_des); // Meters
    declare_parameter("lidar_variance", -1.0, lidar_variance_des); // Meters^2
    declare_parameter("lidar_min_range", -1.0, lidar_min_range_des); // Meters
    declare_parameter("lidar_max_range", -1.0, lidar_max_range_des); // Meters
    declare_parameter("lidar_angle_increment", -1.0, lidar_angle_increment_des); // Degrees
    declare_parameter("lidar_num_samples", -1.0, lidar_num_samples_des);
    declare_parameter("lidar_resolution", -1.0, lidar_resolution_des); // Meters
    
    // Get params - Read params from yaml file that is passed in the launch file
    seed_ = get_parameter("seed").get_parameter_value().get<int>();
    num_robots_ = get_parameter("num_robots").get_parameter_value().get<int>();
    rate = get_parameter("rate").get_parameter_value().get<int>();
    arena_x_min_ = get_parameter("arena_x_min").get_parameter_value().get<double>();
    arena_x_max_ = get_parameter("arena_x_max").get_parameter_value().get<double>();
    arena_y_min_ = get_parameter("arena_y_min").get_parameter_value().get<double>();
    arena_y_max_ = get_parameter("arena_y_max").get_parameter_value().get<double>();
    arena_y_max_ = get_parameter("arena_y_max").get_parameter_value().get<double>();
    min_corridor_width_ = get_parameter("min_corridor_width").get_parameter_value().get<double>();
    wall_breadth_ = get_parameter("wall_breadth").get_parameter_value().get<double>();
    wall_length_ = get_parameter("wall_length").get_parameter_value().get<double>();
    wall_num_ = get_parameter("wall_num").get_parameter_value().get<int>();
    wheel_radius_ = get_parameter("wheel_radius").get_parameter_value().get<double>();
    track_width_ = get_parameter("track_width").get_parameter_value().get<double>();
    encoder_ticks_per_rad_ =
      get_parameter("encoder_ticks_per_rad").get_parameter_value().get<double>();
    motor_cmd_per_rad_sec_ =
      get_parameter("motor_cmd_per_rad_sec").get_parameter_value().get<double>();
    input_noise_ = get_parameter("input_noise").get_parameter_value().get<double>();
    slip_fraction_ = get_parameter("slip_fraction").get_parameter_value().get<double>();
    collision_radius_ = get_parameter("collision_radius").get_parameter_value().get<double>();
    lidar_variance_ = get_parameter("lidar_variance").get_parameter_value().get<double>();
    lidar_min_range_ = get_parameter("lidar_min_range").get_parameter_value().get<double>();
    lidar_max_range_ = get_parameter("lidar_max_range").get_parameter_value().get<double>();
    lidar_angle_increment_ = get_parameter("lidar_angle_increment").get_parameter_value().get<double>();
    lidar_num_samples_ = get_parameter("lidar_num_samples").get_parameter_value().get<double>();
    lidar_resolution_ = get_parameter("lidar_resolution").get_parameter_value().get<double>();
    lidar_frequency_ = 5.0;
    
    // Check all params
    check_yaml_params();

    // Initialize the noise generators
    motor_control_noise_ = std::normal_distribution<>{0.0, std::sqrt(input_noise_)}; // Uncertainity in motor control
    wheel_slip_ = std::uniform_real_distribution<>{-slip_fraction_, slip_fraction_}; // Wheel slipping
    lidar_noise_ = std::normal_distribution<>{0.0, std::sqrt(lidar_variance_)};

    // Timer timestep [seconds]
    dt_ = 1.0 / static_cast<double>(rate);

    // Initialize Pseudo Random environment
    std::srand((unsigned) seed_);

    // Assuming max and min to be integers for simplicity
    arena_x_ = static_cast<double>(std::rand() % static_cast<int>(arena_x_max_ - arena_x_min_)) + arena_x_min_;
    arena_y_ = static_cast<double>(std::rand() % static_cast<int>(arena_y_max_ - arena_y_min_)) + arena_y_min_;

    // Create arena
    create_arena_walls();

    // Create obstacles
    create_walls();

    // Initialize Pseudo Random Turtles

    double x0, y0, theta0;
    for (int i = 0; i < num_robots_; i++)
    {
      // Select random empty spawn point
      std::pair<int, int> spawn_point = empty_spawn_points_.at(std::rand() % static_cast<int>(empty_spawn_points_.size()));
      spawn_points_.push_back(spawn_point);

      // Infer pseudo random pose from selection
      x0 = min_corridor_width_ * 0.5 * static_cast<double>(spawn_point.first - 1) - (arena_x_ - 1.0) / 2.0;
      y0 = min_corridor_width_ * 0.5 * static_cast<double>(spawn_point.second - 1) - (arena_y_ - 1.0) / 2.0;
      theta0 = static_cast<double>(std::rand() % 4) * turtlelib::PI / 2.0;
    
      // Initialize the differential drive kinematic state
      turtles_.push_back(turtlelib::DiffDrive{wheel_radius_, track_width_, turtlelib::wheelAngles{}, turtlelib::Pose2D{theta0, x0, y0}});
    }

    // Create ~/timestep publisher
    timestep_publisher_ = create_publisher<std_msgs::msg::UInt64>("~/timestep", 10);
    // Create ~/obstacles publisher
    walls_publisher_ =
      create_publisher<visualization_msgs::msg::MarkerArray>("~/walls", 10);
    // Create ~/walls publisher
    arena_walls_publisher_ =
      create_publisher<visualization_msgs::msg::MarkerArray>("~/arena_walls", 10);
    // Create red/sensor_data publisher
    sensor_data_publisher_ = create_publisher<nuturtlebot_msgs::msg::SensorData>(
      "red/sensor_data", 10);
    
    for(int i = 0; i < num_robots_; i++)
    {
      // Create color/path publishers
      nav_path_publishers_.push_back(create_publisher<nav_msgs::msg::Path>(colors_.at(i) + "/path", 10));
      paths_.push_back(nav_msgs::msg::Path{});

      // Create color/fake_lidar_scan
      fake_lidar_publishers_.push_back(create_publisher<sensor_msgs::msg::LaserScan>(colors_.at(i) + "/fake_lidar_scan", 10));
      lidars_data_.push_back(sensor_msgs::msg::LaserScan{});
    }

    // Create ~/reset service
    reset_server_ = create_service<std_srvs::srv::Empty>(
      "~/reset",
      std::bind(&Multisim::reset_callback, this, std::placeholders::_1, std::placeholders::_2));
    // Create ~/teleport service
    teleport_server_ = create_service<multisim::srv::Teleport>(
      "~/teleport",
      std::bind(&Multisim::teleport_callback, this, std::placeholders::_1, std::placeholders::_2));

    // Initialize the transform broadcaster
    tf_broadcaster_ = std::make_unique<tf2_ros::TransformBroadcaster>(*this);

    // Create Timer
    timer_ = create_wall_timer(
      std::chrono::milliseconds(1000 / rate),
      std::bind(&Multisim::timer_callback, this));

    // Create red/wheel_cmd
    wheel_cmd_subscriber_ = create_subscription<nuturtlebot_msgs::msg::WheelCommands>(
      "red/wheel_cmd", 10, std::bind(
        &Multisim::wheel_cmd_callback, this,
        std::placeholders::_1));
  }

private:
  // Variables related to environment
  unsigned int seed_;
  unsigned int max_seed_ = 100;
  int num_robots_;
  size_t timestep_;
  int rate;
  std::unique_ptr<tf2_ros::TransformBroadcaster> tf_broadcaster_;
  double dt_ = 0.0; // Multisim Timer in seconds
  
  // Environment
  double arena_x_;
  double arena_x_min_ = 0.0;    // Minimum length of the arena along x [m]
  double arena_x_max_ = 0.0;    // Maxmimum length of the arena along x [m]
  double arena_y_;
  double arena_y_min_ = 0.0;  // Minimum length of the arena along y [m]
  double arena_y_max_ = 0.0;  // Maxmimum length of the arena along y [m]
  double min_corridor_width_ = 0.0;  // Minimum length of any corridor [m]
  double wall_breadth_ = 0.0;  // Breadth/thickness of walls [m]
  double wall_length_ = 0.0;  // Length of walls [m]
  int wall_num_ = 0;  // Number
  double wall_height_ = 0.25;   // Height of walls [m]
  visualization_msgs::msg::MarkerArray arena_walls_;
  visualization_msgs::msg::MarkerArray walls_;
  std::vector<std::pair<int, int>> empty_spawn_points_;
  std::vector<std::pair<int, int>> spawn_points_;

  // Variables related to diff drive
  double wheel_radius_ = -1.0;
  double track_width_ = -1.0;
  nuturtlebot_msgs::msg::SensorData current_sensor_data_; // Encoder ticks
  nuturtlebot_msgs::msg::SensorData prev_sensor_data_; // Encoder ticks
  double encoder_ticks_per_rad_;
  double motor_cmd_per_rad_sec_;
  std::vector<turtlelib::DiffDrive> turtles_;
  std::vector<std::string> colors_ = {"cyan", "magenta", "yellow", "red", "green", "blue", "orange", "brown", "white"};

  // Variables related to visualization
  geometry_msgs::msg::PoseStamped path_pose_stamped_;
  std::vector<nav_msgs::msg::Path> paths_;
  int path_frequency_ = 100; // per timer callback

  // Variables related to noise and sensing
  double input_noise_;
  std::normal_distribution<> motor_control_noise_{0.0, 0.0};
  double slip_fraction_;
  std::uniform_real_distribution<> wheel_slip_{0.0, 0.0};
  double max_range_;
  visualization_msgs::msg::MarkerArray sensed_obstacles_;
  double collision_radius_;
  bool lie_group_collision_ = true;
  bool colliding_ = false;
  std::vector<sensor_msgs::msg::LaserScan> lidars_data_;
  double lidar_variance_;
  double lidar_min_range_;
  double lidar_max_range_;
  double lidar_angle_increment_;
  double lidar_num_samples_;
  double lidar_resolution_;
  double lidar_frequency_;
  std::normal_distribution<> lidar_noise_{0.0, 0.0};

  // Create objects
  rclcpp::TimerBase::SharedPtr timer_;
  rclcpp::Publisher<std_msgs::msg::UInt64>::SharedPtr timestep_publisher_;
  rclcpp::Publisher<visualization_msgs::msg::MarkerArray>::SharedPtr walls_publisher_;
  rclcpp::Publisher<visualization_msgs::msg::MarkerArray>::SharedPtr arena_walls_publisher_;
  rclcpp::Publisher<nuturtlebot_msgs::msg::SensorData>::SharedPtr sensor_data_publisher_;
  rclcpp::Service<std_srvs::srv::Empty>::SharedPtr reset_server_;
  rclcpp::Service<multisim::srv::Teleport>::SharedPtr teleport_server_;
  rclcpp::Subscription<nuturtlebot_msgs::msg::WheelCommands>::SharedPtr wheel_cmd_subscriber_;
  std::vector<rclcpp::Publisher<nav_msgs::msg::Path>::SharedPtr> nav_path_publishers_;
  std::vector<rclcpp::Publisher<sensor_msgs::msg::LaserScan>::SharedPtr> fake_lidar_publishers_;

  /// \brief Reset the simulation
  void reset_callback(
    std_srvs::srv::Empty::Request::SharedPtr,
    std_srvs::srv::Empty::Response::SharedPtr)
  {
    timestep_ = 0;
    turtles_.at(0).q.x = 0.0;
    turtles_.at(0).q.y = 0.0;
    turtles_.at(0).q.theta = 0.0;
  }

  /// \brief Teleport the robot to a specified pose
  void teleport_callback(
    multisim::srv::Teleport::Request::SharedPtr request,
    multisim::srv::Teleport::Response::SharedPtr)
  {
    turtles_.at(0).q.x = request->x;
    turtles_.at(0).q.y = request->y;
    turtles_.at(0).q.theta = request->theta;
  }

  /// \brief Broadcast the TF frames of the robot
  void broadcast_all_turtles()
  {
    for(int i = 0; i < num_robots_; i++)
    // for(int i = 0; i < 3; i++)
    {
      geometry_msgs::msg::TransformStamped t_;

      t_.header.stamp = get_clock()->now();
      t_.header.frame_id = "multisim/world";
      t_.child_frame_id = colors_.at(i) + "/base_footprint";
      t_.transform.translation.x = turtles_.at(i).pose().x;
      t_.transform.translation.y = turtles_.at(i).pose().y;
      t_.transform.translation.z = 0.0;     

      tf2::Quaternion q_;
      q_.setRPY(0, 0, turtles_.at(i).pose().theta);     
      t_.transform.rotation.x = q_.x();
      t_.transform.rotation.y = q_.y();
      t_.transform.rotation.z = q_.z();
      t_.transform.rotation.w = q_.w();

      // Send the transformation
      tf_broadcaster_->sendTransform(t_);
    }

    if (timestep_ % path_frequency_ == 1) {
      update_all_NavPaths();
    }
  }

  /// \brief Create obstacles as a MarkerArray and publish them to a topic to display them in Rviz
  void create_walls()
  {

    for (size_t i = 0; i < static_cast<size_t>(wall_num_); i++)
    {
      visualization_msgs::msg::Marker wall_;
      wall_.header.frame_id = "multisim/world";
      wall_.header.stamp = get_clock()->now();
      wall_.id = i;
      wall_.type = visualization_msgs::msg::Marker::CUBE;
      wall_.action = visualization_msgs::msg::Marker::ADD;

      bool horizontal = std::rand() % 2 == 1 ? true : false;

      if (horizontal)
      {
        // Position
        wall_.pose.position.x = min_corridor_width_ * (std::rand() % (static_cast<int>((arena_x_ - 1.0) / min_corridor_width_) + 1)) - (arena_x_ - 1.0) / 2.0;
        wall_.pose.position.y = min_corridor_width_ * (std::rand() % (static_cast<int>((arena_y_ - 1.0) / min_corridor_width_) + 1)) - (arena_y_ - 1.0) / 2.0;

        // Wall dimensions
        wall_.scale.x = wall_length_;   
        wall_.scale.y = wall_breadth_;  
      }
      else
      {
        // Position
        wall_.pose.position.x = min_corridor_width_ * (std::rand() % (static_cast<int>((arena_x_ - 1.0) / min_corridor_width_) + 1)) - (arena_x_ - 1.0) / 2.0;
        wall_.pose.position.y = min_corridor_width_ * (std::rand() % (static_cast<int>((arena_y_ - 1.0) / min_corridor_width_) + 1)) - (arena_y_ - 1.0) / 2.0;

        // Wall dimensions
        wall_.scale.x = wall_breadth_;   
        wall_.scale.y = wall_length_;  
      }

      // Orientation
      wall_.pose.orientation.x = 0.0;
      wall_.pose.orientation.y = 0.0;
      wall_.pose.orientation.z = 0.0;
      wall_.pose.orientation.w = 1.0;

      // Height
      wall_.pose.position.z = wall_height_ / 2.0;
      wall_.scale.z = wall_height_;         // Height

      // Color
      wall_.color.r = 1.0f;
      wall_.color.g = 0.0f;
      wall_.color.b = 0.0f;
      wall_.color.a = 1.0;

      // Temporarily add wall to MarkerArray
      walls_.markers.push_back(wall_);

      if (!check_connectedness(i))
      {
        walls_.markers.pop_back();
        --i;
      } 
    }
  }

  bool check_connectedness(const size_t wall_index)
  {
    // Create a temporary room grid to represent the state of the room
    std::vector<std::vector<int>> room_grid(static_cast<int>(arena_x_ * 2.0 / min_corridor_width_) - 1, std::vector<int>(static_cast<int>(arena_y_ * 2.0 / min_corridor_width_) - 1, 0));

    // For current wall length to breadth ratio
    static const int wall_dx[] = {-2, -1, 0, 1, 2}; 
    static const int wall_dy[] = {-2, -1, 0, 1, 2};
    static const double multiplier = 2.0 / min_corridor_width_;
    static const int occupancy = sizeof(wall_dx) / sizeof(int);

    // Populate grid with walls
    int wall_x, wall_y;
    for (size_t i = 0; i < walls_.markers.size(); i++)
    {
      wall_x = static_cast<int>(walls_.markers.at(i).pose.position.x * multiplier) + static_cast<int>(arena_x_ / min_corridor_width_) - 1;
      wall_y = static_cast<int>(walls_.markers.at(i).pose.position.y * multiplier) + static_cast<int>(arena_y_ / min_corridor_width_) - 1;

      // Horizontal Walls
      if(walls_.markers.at(i).scale.x == wall_length_)
      {
        for(int j = 0; j < occupancy; j++)
        {
          room_grid[
                      std::min(std::max(0, wall_x + wall_dx[j]), static_cast<int>(arena_x_ * 2.0 / min_corridor_width_) - 2)
                    ]
                    [
                      std::min(std::max(0, wall_y), static_cast<int>(arena_y_ * 2.0 / min_corridor_width_) - 2)
                    ] = 1;
        }
      }
      // Vertical Walls
      else if(walls_.markers.at(i).scale.y == wall_length_)
      {
        for(int j = 0; j < occupancy; j++)
        {
          room_grid[
                      std::min(std::max(0, wall_x), static_cast<int>(arena_x_ * 2.0 / min_corridor_width_) - 2)
                    ]
                    [
                      std::min(std::max(0, wall_y + wall_dy[j]), static_cast<int>(arena_y_ * 2.0 / min_corridor_width_) - 2)
                    ] = 1;
        }
      }
    }

    // Perform BFS to check connectivity
    std::queue<std::pair<int, int>> bfs_queue;

    bool stop = false;
    // Start from an arbitrary free cell
    for(int i = 0; i < static_cast<int>(room_grid.size()); i++)
    {
      for (int j = 0; j < static_cast<int>(room_grid[0].size()); j++)
      {
        if (room_grid[i][j] == 0)
        {
          bfs_queue.push({i, j});
          room_grid[i][j] = 2;
          stop = true;
          break;
        }
      }
      if(stop)
      {
        break;
      }
    }
    if (bfs_queue.empty())
    {
      throw std::runtime_error("Too many walls requested!");
    }

    int ctr = 0;
    while (!bfs_queue.empty())
    {
      auto [x, y] = bfs_queue.front();
      bfs_queue.pop();

      static const int dx[] = {1, 0, -1, 0};
      static const int dy[] = {0, 1, 0, -1};

      for (int i = 0; i < 4; ++i)
      {
        int nx = x + dx[i];
        int ny = y + dy[i];

        if ((nx >= 0 && nx <= static_cast<int>(arena_x_ * 2.0 / min_corridor_width_) - 2) &&
            (ny >= 0 && ny <= static_cast<int>(arena_y_ * 2.0 / min_corridor_width_) - 2) && 
            (room_grid[nx][ny] == 0))
        {
          bfs_queue.push({nx, ny});
          room_grid[nx][ny] = 2;
        }
      }
      ctr++;
    }

    // Check if any cell is still unexplored
    for(int i = 0; i < static_cast<int>(room_grid.size()); i++)
    {
      for (int j = 0; j < static_cast<int>(room_grid[0].size()); j++)
      {
        if (room_grid[i][j] == 0)
        {
          return false; // The wall disconnects the room
        }
      }
    }

    // Check if last wall has been added
    if (wall_index == static_cast<size_t>(wall_num_ - 1))
    {
      // Populate empty spaces
      for(int i = 0; i < static_cast<int>(room_grid.size()); i++)
      {
        for (int j = 0; j < static_cast<int>(room_grid[0].size()); j++)
        {
          if (room_grid[i][j] == 2)
          {
            empty_spawn_points_.push_back({i,j}); // Transpose
            // empty_spawn_points_.push_back({j,i}); // Transpose
          }
        }
      }
      printVector2D(room_grid);
    }

    // Finally
    return true; 
  }

  // Function to print a 2D vector
  void printVector2D(const std::vector<std::vector<int>>& vec) {
      // Iterate over each row
      for (const auto& row : vec) {
          std::string rowString;
          // Iterate over each element in the row and construct a string
          for (int element : row) {
              rowString += std::to_string(element) + " ";
          }
          // Print the row as an error message
          RCLCPP_ERROR(rclcpp::get_logger("print_vector"), "%s", rowString.c_str());
      }
  }

  /// \brief Create walls as a MarkerArray and publish them to a topic to display them in Rviz
  void create_arena_walls()
  {
    for (int i = 0; i <= 3; i++) {
      visualization_msgs::msg::Marker arena_wall_;
      arena_wall_.header.frame_id = "multisim/world";
      arena_wall_.header.stamp = get_clock()->now();
      arena_wall_.id = i;
      arena_wall_.type = visualization_msgs::msg::Marker::CUBE;
      arena_wall_.action = visualization_msgs::msg::Marker::ADD;

      // Wall on positive x-axis
      if (i == 0) {
        arena_wall_.pose.position.x = (arena_x_ + wall_breadth_) / 2.0;
        arena_wall_.pose.position.y = 0.0;

        arena_wall_.pose.orientation.x = 0.0;
        arena_wall_.pose.orientation.y = 0.0;
        arena_wall_.pose.orientation.z = 0.7071068;
        arena_wall_.pose.orientation.w = 0.7071068;
      }
      // Wall on positive y-axis
      else if (i == 1) {
        arena_wall_.pose.position.x = 0.0;
        arena_wall_.pose.position.y = (arena_y_ + wall_breadth_) / 2.0;

        arena_wall_.pose.orientation.x = 0.0;
        arena_wall_.pose.orientation.y = 0.0;
        arena_wall_.pose.orientation.z = 0.0;
        arena_wall_.pose.orientation.w = 1.0;
      }
      // Wall on negative x-axis
      else if (i == 2) {
        arena_wall_.pose.position.x = -(arena_x_ + wall_breadth_) / 2.0;
        arena_wall_.pose.position.y = 0.0;

        arena_wall_.pose.orientation.x = 0.0;
        arena_wall_.pose.orientation.y = 0.0;
        arena_wall_.pose.orientation.z = 0.7071068;
        arena_wall_.pose.orientation.w = 0.7071068;
      }
      // Wall on negative y-axis
      else if (i == 3) {
        arena_wall_.pose.position.x = 0.0;
        arena_wall_.pose.position.y = -(arena_y_ + wall_breadth_) / 2.0;

        arena_wall_.pose.orientation.x = 0.0;
        arena_wall_.pose.orientation.y = 0.0;
        arena_wall_.pose.orientation.z = 0.0;
        arena_wall_.pose.orientation.w = 1.0;
      }
      // Z - Position
      arena_wall_.pose.position.z = wall_height_ / 2.0;

      // Wall dimensions
      if (i == 0 || i == 2) {
        arena_wall_.scale.x = arena_y_ + 2 * wall_breadth_;
      } else {
        arena_wall_.scale.x = arena_x_ + 2 * wall_breadth_;
      }
      arena_wall_.scale.y = wall_breadth_;
      arena_wall_.scale.z = wall_height_;

      // Red Walls
      arena_wall_.color.r = 1.0f;
      arena_wall_.color.g = 0.0f;
      arena_wall_.color.b = 0.0f;
      arena_wall_.color.a = 1.0;

      // Add wall to array
      arena_walls_.markers.push_back(arena_wall_);
    }
  }

  /// \brief wheel_cmd_callback subscription
  void wheel_cmd_callback(const nuturtlebot_msgs::msg::WheelCommands & msg)
  {    

    nuturtlebot_msgs::msg::WheelCommands noisy_wheel_cmd_;
    
    // Add process noise if wheel is moving
    if(msg.left_velocity != 0.0)
    {
      noisy_wheel_cmd_.left_velocity = msg.left_velocity + motor_control_noise_(get_random());
    }
    else
    {
      noisy_wheel_cmd_.left_velocity = msg.left_velocity;
    }

    if(msg.right_velocity != 0.0)
    {
      noisy_wheel_cmd_.right_velocity = msg.right_velocity + motor_control_noise_(get_random());
    }
    else
    {
      noisy_wheel_cmd_.right_velocity = msg.right_velocity;
    }

    // Get current sensor data timestamp
    current_sensor_data_.stamp = get_clock()->now();

    // Update current sensor data as integer values
    current_sensor_data_.left_encoder = round((static_cast<double>(noisy_wheel_cmd_.left_velocity) * motor_cmd_per_rad_sec_) * encoder_ticks_per_rad_ * dt_ + prev_sensor_data_.left_encoder);
    current_sensor_data_.right_encoder = round((static_cast<double>(noisy_wheel_cmd_.right_velocity) * motor_cmd_per_rad_sec_) * encoder_ticks_per_rad_ * dt_ + prev_sensor_data_.right_encoder);

    // Set previous as current    
    prev_sensor_data_ = current_sensor_data_;

    // Simulate slipping
    double left_slip_ = wheel_slip_(get_random());  // Add slip to wheel position
    double right_slip_ = wheel_slip_(get_random());

    // Change in wheel angles with slip
    turtlelib::wheelAngles delta_wheels_{(static_cast<double>(noisy_wheel_cmd_.left_velocity) * (1 + left_slip_) * motor_cmd_per_rad_sec_) * dt_, (static_cast<double>(noisy_wheel_cmd_.right_velocity) * (1 + right_slip_) * motor_cmd_per_rad_sec_) * dt_};

    // Detect and perform required update to transform if colliding, otherwise update trasnform normally
    // if(!detect_and_simulate_collision(delta_wheels_))
    // {
    //   // Update Transform if no collision
    //   turtle_.driveWheels(delta_wheels_);
    // }
  }

  /// \brief Publish sensor data
  void sensor_data_pub()
  {    
    sensor_data_publisher_->publish(current_sensor_data_);
  }

  // /// \brief Update Simulated turtle's nav path.
  void update_all_NavPaths()
  {
    for (int i = 0; i < num_robots_; i++)
    {
      // Update ground truth red turtle path
      paths_.at(i).header.stamp = get_clock()->now();
      paths_.at(i).header.frame_id = "multisim/world";
      // Create new pose stamped
      path_pose_stamped_.header.stamp = get_clock()->now();
      path_pose_stamped_.header.frame_id = "multisim/world";
      path_pose_stamped_.pose.position.x = turtles_.at(i).pose().x;
      path_pose_stamped_.pose.position.y = turtles_.at(i).pose().y;
      path_pose_stamped_.pose.position.z = 0.0;
      tf2::Quaternion q_;
      q_.setRPY(0, 0, turtles_.at(i).pose().theta);     // Rotation around z-axis
      path_pose_stamped_.pose.orientation.x = q_.x();
      path_pose_stamped_.pose.orientation.y = q_.y();
      path_pose_stamped_.pose.orientation.z = q_.z();
      path_pose_stamped_.pose.orientation.w = q_.w();
      // Append pose stamped
      paths_.at(i).poses.push_back(path_pose_stamped_);
    }
  }

  // /// \brief Indicate and handle collisions
  // bool detect_and_simulate_collision(turtlelib::wheelAngles predicted_delta_wheels_)
  // {    
  //   // Predicted robot motion
  //   turtlelib::DiffDrive predicted_turtle_ = turtle_;
  //   predicted_turtle_.driveWheels(predicted_delta_wheels_);   

  //   turtlelib::Transform2D T_world_robot_{{predicted_turtle_.pose().x, predicted_turtle_.pose().y}, predicted_turtle_.pose().theta};
  //   turtlelib::Transform2D T_robot_world_ = T_world_robot_.inv(); 

  //   for (size_t i = 0; i < obstacles_x_.size(); i++)
  //   {
  //     // Find local coordinates of obstacle
  //     turtlelib::Point2D obstacle_pos_world_{obstacles_x_.at(i), obstacles_y_.at(i)};
  //     turtlelib::Point2D obstacle_pos_robot_ = T_robot_world_(obstacle_pos_world_);

  //     // Detect collision
  //     if (std::sqrt(std::pow(obstacle_pos_robot_.x, 2) + std::pow(obstacle_pos_robot_.y, 2)) < (collision_radius_ + obstacles_r_)) 
  //     {
  //       // If colliding, calculate shift of robot frame, in robot frame
  //       turtlelib::Vector2D robotshift_robot_{
  //       -((collision_radius_ + obstacles_r_) * cos(atan2(obstacle_pos_robot_.y, obstacle_pos_robot_.x)) - obstacle_pos_robot_.x),
  //       -((collision_radius_ + obstacles_r_) * sin(atan2(obstacle_pos_robot_.y, obstacle_pos_robot_.x)) - obstacle_pos_robot_.y)
  //       };

  //       // Calculate transform corresponding to this shift
  //       turtlelib::Transform2D T_robot_newrobot_{robotshift_robot_};

  //       // Define this transformation in the world frame
  //       turtlelib::Transform2D T_world_newrobot_ = T_world_robot_ * T_robot_newrobot_;

  //       if(lie_group_collision_)
  //       {
  //         turtle_.q.x = T_world_newrobot_.translation().x;
  //         turtle_.q.y = T_world_newrobot_.translation().y;
  //         turtle_.q.theta = T_world_newrobot_.rotation();
  //       }
          
  //       turtle_.phi.left = turtlelib::normalize_angle(turtle_.phi.left + predicted_delta_wheels_.left); // TODO: wheel rotation not working properly
  //       turtle_.phi.right = turtlelib::normalize_angle(turtle_.phi.right + predicted_delta_wheels_.right);

  //       RCLCPP_DEBUG(this->get_logger(), "turtle: %f B: %f", obstacle_pos_robot_.x, obstacle_pos_robot_.y);
  //       return true; // Colliding with one obstacle, therefore, ignore other obstacles
  //     } 
  //   }
  //   return false; // Not colliding

  //   throw std::runtime_error("Invalid collision! Check collision simulation!");
  // }

  /// \brief Fake lidar data
  void lidar()
  {
    for(int i = 0; i < num_robots_; i++)
    {
      lidars_data_.at(i).header.frame_id = colors_.at(i) + "/base_scan";
      lidars_data_.at(i).header.stamp = get_clock()->now();
      lidars_data_.at(i).angle_min = 0.0;
      lidars_data_.at(i).angle_max = turtlelib::deg2rad(360.0); // convert degrees to radians
      lidars_data_.at(i).angle_increment = turtlelib::deg2rad(lidar_angle_increment_); // convert degrees to radians
      lidars_data_.at(i).time_increment = 0.0005574136157520115;
      lidars_data_.at(i).time_increment = 0.0;
      lidars_data_.at(i).scan_time = 1.0 / lidar_frequency_;
      lidars_data_.at(i).range_min = lidar_min_range_;
      lidars_data_.at(i).range_max = lidar_max_range_;
      lidars_data_.at(i).ranges.resize(lidar_num_samples_);

      // Offset between LIDAR and Footprint (fixed, unless things go very ugly)
      turtlelib::Pose2D lidar_pose_{turtles_.at(i).pose().theta, turtles_.at(i).pose().x - 0.032*cos(turtles_.at(i).pose().theta), turtles_.at(i).pose().y - 0.032*sin(turtles_.at(i).pose().theta)};

      // Iterate over samples
      for (int sample_index = 0; sample_index < lidar_num_samples_; sample_index++) 
      {       
        // Limit of laser in world frame
        turtlelib::Point2D limit{
                                  lidar_pose_.x + lidar_max_range_ * cos(sample_index * lidars_data_.at(i).angle_increment + lidar_pose_.theta),
                                  lidar_pose_.y + lidar_max_range_ * sin(sample_index * lidars_data_.at(i).angle_increment + lidar_pose_.theta)
                                };

        // Slope of laser trace in world frame
        double slope = (limit.y - lidar_pose_.y) / (limit.x - lidar_pose_.x  + 1e-7);

        double lidar_reading = lidar_max_range_;

        // Determine intersection between laser and closest wall

        // 1. For randomly placed walls
        for (size_t j = 0; j < walls_.markers.size(); j++) 
        {
          // Determine which kind of wall
          double x_len = 0;
          double y_len = 0;
          if(walls_.markers.at(j).scale.x == wall_length_)
          {
            // Horizontal
            x_len = wall_length_;
            y_len = wall_breadth_;
          }
          else if(walls_.markers.at(j).scale.y == wall_length_)
          {
            // Vertical
            x_len = wall_breadth_;
            y_len = wall_length_;
          }

          // West face
          if ((turtles_.at(i).pose().x < walls_.markers.at(j).pose.position.x - x_len/2.0) && 
              (walls_.markers.at(j).pose.position.x - x_len/2.0 < limit.x))
          {
            // Check if y_intercept lies on the wall
            double y_intercept = turtles_.at(i).pose().y + slope * (walls_.markers.at(j).pose.position.x - x_len/2.0 - turtles_.at(i).pose().x);

            if (std::fabs(y_intercept - walls_.markers.at(j).pose.position.y) < y_len / 2.0)
            {
              turtlelib::Vector2D laser_vector{(walls_.markers.at(j).pose.position.x - x_len/2.0 - turtles_.at(i).pose().x), 0};
              laser_vector.y = laser_vector.x * slope;

              lidar_reading = std::min(lidar_reading, turtlelib::magnitude(laser_vector));
            }
          }

          // East face
          if ((limit.x < walls_.markers.at(j).pose.position.x + x_len/2.0) &&
              (walls_.markers.at(j).pose.position.x + x_len/2.0 < turtles_.at(i).pose().x))
          {
            // Check if y_intercept lies on the wall
            double y_intercept = turtles_.at(i).pose().y + slope * (walls_.markers.at(j).pose.position.x + x_len/2.0 - turtles_.at(i).pose().x);

            if (std::fabs(y_intercept - walls_.markers.at(j).pose.position.y) < y_len / 2.0)
            {
              turtlelib::Vector2D laser_vector{(walls_.markers.at(j).pose.position.x + x_len/2.0 - turtles_.at(i).pose().x), 0};
              laser_vector.y = laser_vector.x * slope;

              lidar_reading = std::min(lidar_reading, turtlelib::magnitude(laser_vector));
            }
          }

          // South face
          if ((turtles_.at(i).pose().y < walls_.markers.at(j).pose.position.y - y_len/2.0) && 
              (walls_.markers.at(j).pose.position.y - y_len/2.0 < limit.y))
          {
            // Check if x_intercept lies on the wall
            double x_intercept = turtles_.at(i).pose().x + (1.0 / (slope + 1e-7)) * (walls_.markers.at(j).pose.position.y - y_len/2.0 - turtles_.at(i).pose().y);

            if (std::fabs(x_intercept - walls_.markers.at(j).pose.position.x) < x_len / 2.0)
            {
              turtlelib::Vector2D laser_vector{0, walls_.markers.at(j).pose.position.y - y_len/2.0 - turtles_.at(i).pose().y};
              laser_vector.x = laser_vector.y / (slope + 1e-7);

              lidar_reading = std::min(lidar_reading, turtlelib::magnitude(laser_vector));
            }
          }

          // North face
          if ((limit.y < walls_.markers.at(j).pose.position.y + y_len/2.0) &&
              (walls_.markers.at(j).pose.position.y + y_len/2.0 < turtles_.at(i).pose().y))
          {
            // Check if y_intercept lies on the wall
            double x_intercept = turtles_.at(i).pose().x + (1.0 / (slope + 1e-7)) * (walls_.markers.at(j).pose.position.y + y_len/2.0 - turtles_.at(i).pose().y);

            if (std::fabs(x_intercept - walls_.markers.at(j).pose.position.x) < x_len / 2.0)
            {
              turtlelib::Vector2D laser_vector{0, walls_.markers.at(j).pose.position.y + y_len/2.0 - turtles_.at(i).pose().y};
              laser_vector.x = laser_vector.y / (slope + 1e-7);

              lidar_reading = std::min(lidar_reading, turtlelib::magnitude(laser_vector));
            }
          }
        }

        // 2. For arena walls

        // North Wall
        if(limit.y > arena_y_/2.0)
        {
          turtlelib::Vector2D laser_vector{ 0, arena_y_/2.0 - lidar_pose_.y};
          laser_vector.x = laser_vector.y / (slope  + 1e-7);

          lidar_reading = std::min(lidar_reading, turtlelib::magnitude(laser_vector));
        }
        // West Wall
        if(limit.x < -arena_x_/2.0)
        {
          turtlelib::Vector2D laser_vector{ -arena_x_/2.0 - lidar_pose_.x, 0};
          laser_vector.y = laser_vector.x * slope;

          lidar_reading = std::min(lidar_reading, turtlelib::magnitude(laser_vector));
        }
        // South Wall
        if(limit.y < -arena_y_/2.0)
        {
          turtlelib::Vector2D laser_vector{ 0, -arena_y_/2.0 - lidar_pose_.y};
          laser_vector.x = laser_vector.y / (slope  + 1e-7);

          lidar_reading = std::min(lidar_reading, turtlelib::magnitude(laser_vector));
        }
        // East Wall
        if(limit.x > arena_x_/2.0)
        {
          turtlelib::Vector2D laser_vector{ arena_x_/2.0 - lidar_pose_.x, 0};
          laser_vector.y = laser_vector.x * slope;

          lidar_reading = std::min(lidar_reading, turtlelib::magnitude(laser_vector));
        }

        // Check lidar ranges
        if (lidar_reading >= lidar_max_range_ || lidar_reading < lidar_min_range_) 
        { 
          lidars_data_.at(i).ranges.at(sample_index) = 0.0;
        } 
        else 
        {
          // Snap to lidar resolution
          lidars_data_.at(i).ranges.at(sample_index) = lidar_resolution_ * round((lidar_reading + lidar_noise_(get_random())) / lidar_resolution_) ;
        }
      }
    }
  }

  /// \brief Main simulation time loop
  void timer_callback()
  {
    auto message = std_msgs::msg::UInt64();
    message.data = ++timestep_;
    timestep_publisher_->publish(message);
    walls_publisher_->publish(walls_);
    arena_walls_publisher_->publish(arena_walls_);

    lidar();
    
    sensor_data_pub();

    broadcast_all_turtles();

    for(int i = 0; i < num_robots_; i++)
    {
      nav_path_publishers_.at(i)->publish(paths_.at(i));
    }

    // Publish at lidar_frequency_ despite the timer frequency
    if (timestep_ % static_cast<int>(rate / lidar_frequency_) == 1)
    {
      for(int i = 0; i < num_robots_; i++)
      {
        fake_lidar_publishers_.at(i)->publish(lidars_data_.at(i));
      }
    }
  }

  /// \brief Ensures all values are passed via .yaml file, and they're reasonable
  void check_yaml_params()
  {
    if (  wheel_radius_ == -1.0 ||
          track_width_ == -1.0 ||
          encoder_ticks_per_rad_ == -1.0 ||
          motor_cmd_per_rad_sec_ == -1.0 ||
          input_noise_ == -1.0 ||
          slip_fraction_ == -1.0 ||
          collision_radius_ == -1.0 ||
          lidar_variance_ == -1.0 ||
          lidar_min_range_ == -1.0 ||
          lidar_max_range_ == -1.0 ||
          lidar_angle_increment_ == -1.0 ||
          lidar_num_samples_ == -1.0 ||
          lidar_resolution_ == -1.0
          )
    {
      RCLCPP_ERROR(this->get_logger(), "Param wheel_radius: %f", wheel_radius_);
      RCLCPP_ERROR(this->get_logger(), "Param track_width: %f", track_width_);
      RCLCPP_ERROR(this->get_logger(), "Param encoder_ticks_per_rad: %f", encoder_ticks_per_rad_);
      RCLCPP_ERROR(this->get_logger(), "Param motor_cmd_per_rad_sec: %f", motor_cmd_per_rad_sec_);
      RCLCPP_ERROR(this->get_logger(), "Param input_noise: %f", input_noise_);
      RCLCPP_DEBUG(this->get_logger(), "Param slip_fraction: %f", slip_fraction_);
      RCLCPP_DEBUG(this->get_logger(), "Param collision_radius: %f", collision_radius_);
      RCLCPP_DEBUG(this->get_logger(), "Param lidar_variance: %f", lidar_variance_);
      RCLCPP_DEBUG(this->get_logger(), "Param lidar_min_range: %f", lidar_min_range_);
      RCLCPP_DEBUG(this->get_logger(), "Param lidar_max_range: %f", lidar_max_range_);
      RCLCPP_DEBUG(this->get_logger(), "Param lidar_angle_increment: %f", lidar_angle_increment_);
      RCLCPP_DEBUG(this->get_logger(), "Param lidar_num_samples: %f", lidar_num_samples_);
      RCLCPP_DEBUG(this->get_logger(), "Param lidar_resolution: %f", lidar_resolution_);
      
      throw std::runtime_error("Missing necessary parameters in diff_params.yaml!");
    }

    if (  wheel_radius_ <= 0.0 ||
          track_width_ <= 0.0 ||
          encoder_ticks_per_rad_ <= 0.0 ||
          motor_cmd_per_rad_sec_ <= 0.0 ||
          input_noise_ < 0.0 ||
          slip_fraction_ < 0.0 ||
          collision_radius_ < 0.0 ||
          lidar_min_range_ < 0.0 ||
          lidar_max_range_ < 0.0 ||
          lidar_angle_increment_ <= 0.0 ||
          lidar_num_samples_ <= 0.0 ||
          lidar_resolution_ < 0.0
          )
    {
      RCLCPP_DEBUG(this->get_logger(), "Param wheel_radius: %f", wheel_radius_);
      RCLCPP_DEBUG(this->get_logger(), "Param track_width: %f", track_width_);
      RCLCPP_DEBUG(this->get_logger(), "Param encoder_ticks_per_rad: %f", encoder_ticks_per_rad_);
      RCLCPP_DEBUG(this->get_logger(), "Param motor_cmd_per_rad_sec: %f", motor_cmd_per_rad_sec_);
      RCLCPP_DEBUG(this->get_logger(), "Param input_noise: %f", input_noise_);
      RCLCPP_DEBUG(this->get_logger(), "Param slip_fraction: %f", slip_fraction_);
      RCLCPP_DEBUG(this->get_logger(), "Param collision_radius: %f", collision_radius_);
      RCLCPP_DEBUG(this->get_logger(), "Param lidar_variance: %f", lidar_variance_);
      RCLCPP_DEBUG(this->get_logger(), "Param lidar_min_range: %f", lidar_min_range_);
      RCLCPP_DEBUG(this->get_logger(), "Param lidar_max_range: %f", lidar_max_range_);
      RCLCPP_DEBUG(this->get_logger(), "Param lidar_angle_increment: %f", lidar_angle_increment_);
      RCLCPP_DEBUG(this->get_logger(), "Param lidar_num_samples: %f", lidar_num_samples_);
      RCLCPP_DEBUG(this->get_logger(), "Param lidar_resolution: %f", lidar_resolution_);
      
      throw std::runtime_error("Incorrect params in diff_params.yaml!");
    }

    if (seed_ == 0)
    {
      throw std::runtime_error("Missing seed value!");
    }
    else if (seed_ > max_seed_)
    {
      RCLCPP_ERROR(this->get_logger(), "Seed: %d", seed_);
      throw std::runtime_error("Improper seed value!");
    }
  }

  /// \brief Calculate the euclidean distance
  /// \param x1 point 1 x-coordinate (double)
  /// \param y1 point 1 y-coordinate (double)
  /// \param x2 point 2 x-coordinate (double)
  /// \param y2 point 2 y-coordinate (double)
  /// \return euclidean distance (double)
  double euclidean_distance(double x1, double y1, double x2, double y2)
  {
    double dx = x2 - x1;
    double dy = y2 - y1;
    return std::sqrt(dx * dx + dy * dy);
  }
};

/// \brief Main function for node create, error handel and shutdown
int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<Multisim>());
  rclcpp::shutdown();
  return 0;
}

#include <vector>
#include <iostream>
#include <random>
#include "lodepng.h"

using namespace std;

typedef unsigned char channel; // using unsigned chars to store integers in the range 0-255 in line with the lodepng standard

struct vec_2d // a simple 2D vector struct which we'll require for the Perlin noise algorithm
{
   double v1 {}, v2 {};

   vec_2d (double x, double y) : v1 {x}, v2 {y} {}
   vec_2d () {}
   double operator * (vec_2d& b)
   {
      return v1 * b.v1 + v2 * b.v2;
   }
};

class grid_2d {
   public:
      grid_2d (size_t width, char* custom_seed) // this constructor first initialises the grid and the grid side length, then populates the grid with random unit vectors
         : grid_side {width / GRID_SCALE + 1},
           grid {vector<vector<vec_2d>> (ceil (grid_side), vector<vec_2d>(ceil (grid_side)))}
      {
         mt19937 mt([custom_seed]() { // using a lambda to return the value of the seed during construction
            if (custom_seed == nullptr) {
               auto ret {[]() {
                  random_device rd;
                  return rd();
               }()};

               cout << "Your seed is: " << ret << '\n';
               return ret;
            } else
               return static_cast<unsigned int>(atoi (custom_seed));
         }());

         uniform_real_distribution<double> dist(-1.0, 1.0);
         auto random_bits {mt()}; // stores a random output from the mersenne twister. during the construction of the grid, the bits of this output are used to determine whether each vector will have a positive or negative v2 value
         int counter {0};

         for (auto& r : grid)
            for (auto& v : r) {
               if (counter == 32) // if we have right shifted the number 32 times (i.e. there are no bits left)...
                  random_bits = mt(), // ...then generate some more in the form of a different number
                  counter = 0;

               auto v1 {dist(mt)}, v2 {(random_bits & 1 ? 1 : -1) * sqrt(1 - v1 * v1)}; // after selecting a random number in the distribution (v1), we calculate the value v2 such that v1^2 + v2^2 = 1, so that v is a unit vector. after rearranging for v2, we find that v2 = sqrt(1 - v1^2). note that we multiply v by either 1 or -1, depending on the value of the rightmost bit of random_bits so that we have an even chance of v2 being either positive or negative - if we didn't do then then v2 would always be non-negative because 0 <= v1 * v1 <= 1 and hence 0 <= 1 - v1 * v1 <= 1

               v = vec_2d {std::move(v1), std::move(v2)}; // initialising v2 beforehand instead of inputting it as an rvalue allows us to move v1 (if we didn't do this then v1 would be used in both parameters and since C++23 has no guarantee on the order in which parameters are initialised, we would not be able to safely move v1)

               random_bits >>= 1; // apply a right shift to random_bits so that we can move the next bit to the right
               ++counter;
            }
      }

      vector<channel> get_img_vec(size_t width)
      {
         vector<channel> img;
         img.reserve(width * width);
         for (double r {POINT_INTERVAL}; r < grid_side - 1; r += POINT_INTERVAL)
            for (double c {POINT_INTERVAL}; c < grid_side - 1; c += POINT_INTERVAL)
               img.emplace_back (to_greyscale (get_coord_val(r, c))); // pushing the pixel at the coordinate (r, c) to the image. using emplace_back because to_greyscale returns a double

         return img;
      }

   private:
      static constexpr double GRID_SCALE = 16.0, POINT_INTERVAL = nextafter(1 / GRID_SCALE, 0);
      // NOTE: the value of POINT_INTERVAL is the solution of the equation floor (h / (GRID_SCALE * POINT_INTERVAL)) = h which works for all values h that is also not equal to 1/GRID_SCALE. POINT_INTERVAL cannot be equal to 1/GRID_SCALE because if it were than we would sample one more row of coordinates than we should (try drawing a 2x2 grid and sampling a coordinate every 1/16 to see this for yourself)
      // also note that constexpr nextafter is only available in C++23 and beyond - this is a C++23 program!

      const double grid_side;
      vector<vector<vec_2d>> grid;

      double get_coord_val (double r, double c)
      {
         auto r_floor {floor(r)}, c_floor {floor(c)};
         auto r_floor_dist {r - r_floor}, c_floor_dist {c - c_floor};

         // the distance vectors. these macros avoid the initialisation of unnecessary lvalues and move semantics when we only need rvalues for our interpolations
         #define top_left_dist     vec_2d (-r_floor_dist, c_floor_dist)
         #define top_right_dist    vec_2d (-r_floor_dist, c_floor_dist - 1)
         #define bottom_left_dist  vec_2d (1 - r_floor_dist, c_floor_dist)
         #define bottom_right_dist vec_2d (1 - r_floor_dist, c_floor_dist - 1)

         return interp // to obtain the coordinate value, we'll calculate the interp for each pair of corners and then calculate the interp of the two results. the vector multiplication is between the distance vectors and the gradient vectors from the grid
         (
            interp (top_left_dist * grid[r_floor][c_floor], top_right_dist * grid[r_floor][c_floor + 1], c_floor_dist), // interp between the top two corners
            interp (bottom_left_dist * grid[r_floor + 1][c_floor], bottom_right_dist * grid[r_floor + 1][c_floor + 1], c_floor_dist), // interp between the bottom two corners
            r_floor_dist // non-negative vertical distance between the top corners and the input cordinate
         );
      }

      double interp (double start, double end, double t) // implementation of the smootherstep algorithm
      {
         return start + t * t * t * (3 * t * (2 * t - 5) + 10) * (end - start);
      }

      static constexpr auto BEFORE_128 = nextafter(128.0, 127);

      double to_greyscale (double d) // maps a double in the range [-1.0, 1.0] to a corresponding double in the range [0, 256). note that upon taking the floor value of this function's output an integer in the range [0, 255] wil be obtained
      {
         return (d + 1) * BEFORE_128; // we use the greatest double less than 128 because if we multiplied by 128 then we would return 256 if we had d = 1
      }
};

int main(int argc, char** argv)
{
   if (argc <= 2 || argc >= 5) {
      cerr << "Error: invalid number of arguments. Please only enter the width and output file name of the desired image, and optionally, the seed.\n";
      return 1;
   }

   size_t width = [argv]() { // a simple lambda to check whether there were issues with parsing the input dimensions with strtoul prior to initialising the width of the image
      auto ret {strtoul (argv[1], nullptr, 10)};
      if (ret == 0) {
         cerr << "Error while parsing input dimension: positive integer not entered.\n";
         exit(1);
      }

      return ret;
   }();

   grid_2d grid (width, argv[3]);

   if (lodepng::encode(argv[2], grid.get_img_vec(width), width, width, LCT_GREY)) {
      cerr << "Error: output image could not be encoded.\n";
      return 2;
   }
}

int main()
{
    struct bar {
       double x, y;
    };

    struct foo {
      std::string name;
      std::vector<bar> points;
      int a;
    };

    std::vector<foo> f{
      { .name = "cat", .points = { {1, 1}, {2, 2} }, .a = 123 },
      { .name = "dog", .points = { {10, 10}, {20, 20} }, .a = 465 },
    };
}

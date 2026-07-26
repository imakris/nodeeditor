#include <QtNodes/BasicGraphicsScene>

#include <type_traits>

int main()
{
    static_assert(std::is_destructible_v<QtNodes::BasicGraphicsScene>);
    return 0;
}

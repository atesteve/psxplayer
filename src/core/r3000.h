#include <memory>
#include <stdexcept>

enum class FaultCheck {
    SOFTWARE,
    HARDWARE,
};

enum class AlignmentCheck {
    NO_CHECK,
    SOFTWARE,
    HARDWARE,
};

enum class AccessType {
    READ,
    WRITE,
};

enum class AccessWidth {
    A8, A16, A32,
};

struct R3000CoreConfig {
    FaultCheck fault_check{};
    AlignmentCheck alignment_check{};
};

template<R3000CoreConfig>
class R3000Core {
    explicit R3000Core();
    ~R3000Core();

    void run();

private:
    struct Private;
    std::unique_ptr<Private> p;
};

class R3000Exception : public std::logic_error {
public:
    using std::logic_error::logic_error;
};

// Explicit instantiation
extern template class R3000Core<{}>;

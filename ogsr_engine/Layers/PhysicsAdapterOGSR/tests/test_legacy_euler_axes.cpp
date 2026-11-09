// Regression guard for the legacy OGSR Euler joint layout.
//
// This test intentionally inspects the unchanged legacy source instead of
// constructing CPHJoint: doing the latter would require linking xrPhysics, ODE
// and the whole engine test stack. The invariants below are the ones the
// diagnostic exporter relies on:
//
// - CPHJoint(full_control) falls through to the slider case and stores five
//   SPHAxis entries;
// - only entries 0..2 are driven by dAMotorEuler;
// - LimitAxisNum clamps user-facing axis indices to 2;
// - the collector exports three active axes and reports the two inert entries.

#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace
{
int gChecks = 0;
int gFailures = 0;

void check(bool condition, const std::string& message)
{
    ++gChecks;
    if (!condition)
    {
        ++gFailures;
        std::cout << "FAIL: " << message << '\n';
    }
}

std::string readFile(const char* path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
        return {};
    std::ostringstream output;
    output << input.rdbuf();
    return output.str();
}

std::size_t countOccurrences(const std::string& text, const std::string& needle)
{
    std::size_t count = 0;
    std::size_t position = 0;
    while ((position = text.find(needle, position)) != std::string::npos)
    {
        ++count;
        position += needle.size();
    }
    return count;
}

void checkLegacyJointSource(const std::string& source)
{
    const std::size_t constructor = source.find("CPHJoint::CPHJoint");
    check(constructor != std::string::npos, "CPHJoint constructor is present");
    if (constructor == std::string::npos)
        return;

    const std::size_t fullControl = source.find("case full_control:", constructor);
    check(fullControl != std::string::npos, "constructor has a full_control case");
    if (fullControl == std::string::npos)
        return;

    const std::size_t slider = source.find("case slider:", fullControl);
    check(slider != std::string::npos, "full_control falls through to slider");
    if (slider == std::string::npos)
        return;

    const std::string fullControlBlock = source.substr(fullControl, slider - fullControl);
    const std::size_t primaryAxes = countOccurrences(fullControlBlock, "axes.push_back");
    check(primaryAxes == 3, "full_control adds exactly three primary axes");
    check(fullControlBlock.find("break") == std::string::npos,
          "full_control has no break before the slider case");

    const std::size_t switchEnd = source.find("\n    }", slider);
    check(switchEnd != std::string::npos, "constructor switch end is present");
    if (switchEnd == std::string::npos)
        return;
    const std::string sliderBlock = source.substr(slider, switchEnd - slider);
    const std::size_t fallthroughAxes = countOccurrences(sliderBlock, "axes.push_back");
    check(fallthroughAxes == 2, "slider fallthrough adds two more entries");
    check(primaryAxes + fallthroughAxes == 5,
          "full_control raw axes vector contains five entries");

    const std::size_t create = source.find("void CPHJoint::CreateFullControl");
    check(create != std::string::npos, "CreateFullControl is present");
    if (create == std::string::npos)
        return;
    const std::size_t createEnd = source.find("\n}", create);
    check(createEnd != std::string::npos, "CreateFullControl body end is present");
    if (createEnd == std::string::npos)
        return;
    const std::string createBody = source.substr(create, createEnd - create);
    check(createBody.find("dJointSetAMotorNumAxes(m_joint1, 3)") != std::string::npos,
          "Euler motor is configured with exactly three axes");
    check(createBody.find("CalcAxis(0") != std::string::npos &&
              createBody.find("CalcAxis(1") != std::string::npos &&
              createBody.find("CalcAxis(2") != std::string::npos,
          "CreateFullControl uses axes 0, 1 and 2");
    check(createBody.find("CalcAxis(3") == std::string::npos &&
              createBody.find("CalcAxis(4") == std::string::npos,
          "CreateFullControl never uses entries 3 or 4");

    const std::size_t limit = source.find("void CPHJoint::LimitAxisNum");
    check(limit != std::string::npos, "LimitAxisNum is present");
    if (limit == std::string::npos)
        return;
    const std::size_t limitEnd = source.find("\n}", limit);
    check(limitEnd != std::string::npos, "LimitAxisNum body end is present");
    if (limitEnd == std::string::npos)
        return;
    const std::string limitBody = source.substr(limit, limitEnd - limit);
    check(limitBody.find("case full_control: axis_num = axis_num > 2 ? 2 : axis_num") !=
              std::string::npos,
          "full_control public axis indices are clamped to 2");

    const std::size_t axesNumber = source.find("u16 CPHJoint::GetAxesNumber");
    check(axesNumber != std::string::npos, "GetAxesNumber is present");
    if (axesNumber != std::string::npos)
        check(source.find("return u16(axes.size());", axesNumber) != std::string::npos,
              "GetAxesNumber returns the raw vector size");
}

void checkCollectorExport(const std::string& source)
{
    check(source.find("std::min<int>(rawOdeAxes, 3)") != std::string::npos,
          "collector explicitly exports only the first three ODE axes");
    check(source.find("legacyInertAxes") != std::string::npos,
          "collector counts the inert legacy entries");
    check(source.find("indices 3-4") != std::string::npos,
          "collector reports the fallthrough reason");
}

void checkSimulatedExport()
{
    constexpr int rawAxes = 5;
    const int activeAxes = std::min(rawAxes, 3);
    const int inertAxes = rawAxes - activeAxes;
    check(activeAxes == 3, "five raw entries export three active Euler axes");
    check(inertAxes == 2, "entries 3 and 4 are not independent degrees of freedom");
    check(activeAxes + inertAxes == rawAxes, "raw and exported axis counts are consistent");
}
} // namespace

int main(int argc, char** argv)
{
    if (argc != 3)
    {
        std::cout << "usage: legacy_euler_axes_tests <PHJoint.cpp> <ogsr_capture.h>\n";
        return 2;
    }

    const std::string jointSource = readFile(argv[1]);
    const std::string collectorSource = readFile(argv[2]);
    check(!jointSource.empty(), "PHJoint.cpp is readable");
    check(!collectorSource.empty(), "ogsr_capture.h is readable");
    if (!jointSource.empty())
        checkLegacyJointSource(jointSource);
    if (!collectorSource.empty())
        checkCollectorExport(collectorSource);
    checkSimulatedExport();

    std::cout << "legacy euler axes tests: " << gChecks << " checks, " << gFailures << " failures\n";
    return gFailures == 0 ? 0 : 1;
}

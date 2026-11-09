#include "physics_physx/physx_ragdoll_world.h"
#include "physics_portable/json.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace
{
using physics_portable::RagdollCaptureDocument;
using physics_portable::RagdollDescription;
using physics_portable::RagdollInitialState;
using physics_portable::RagdollSimulationConfig;
using physics_portable::Vec3;
using physics_physx::PhysXRagdollWorld;
using physics_physx::RagdollDiagnostics;

constexpr int kSteps = 600;
constexpr float kMaxAnchorError = 0.05f;
constexpr float kMaxLimitError = 0.2f;
constexpr double kMaxFinalKinetic = 1.0;

enum class ImpulsePoint
{
    CenterOfMass,
    BodyOrigin
};

struct Variant
{
    std::string name;
    bool required = false;
    bool impulse = false;
    physics_portable::BodyId impulseBody = 0;
    ImpulsePoint impulsePoint = ImpulsePoint::CenterOfMass;
    bool disableSelfCollision = true;
    float linearDamping = 0.05f;
    float angularDamping = 0.2f;
    std::uint32_t solverPosition = 8;
    std::uint32_t solverVelocity = 2;
    bool legacyRoot = false;
    float rootDensity = 300.0f;
    float impulseMagnitude = 8.0f;
};

struct Summary
{
    std::string name;
    bool required = false;
    bool technicalOk = false;
    bool acceptanceOk = false;
    float totalMass = 0.0f;
    float rootMass = 0.0f;
    double maxKinetic = 0.0;
    double finalKinetic = 0.0;
    double maxEnergy = 0.0;
    float maxAngular = 0.0f;
    float maxAnchor = 0.0f;
    float maxLimit = 0.0f;
    float maxPenetration = 0.0f;
    std::uint32_t maxContacts = 0;
    std::uint32_t maxFound = 0;
    std::uint32_t maxPersist = 0;
    std::uint32_t maxLost = 0;
    std::uint32_t maxActivePairs = 0;
    std::uint32_t awakeAtEnd = 0;
    std::uint32_t stepsCompleted = 0;
};

bool finite(double value) { return std::isfinite(value); }

bool buildGround(PhysXRagdollWorld& world, const RagdollInitialState& initial, float& groundY, std::string& error)
{
    groundY = initial.bodyWorld.front().translation.y;
    for (const auto& transform : initial.bodyWorld)
        groundY = std::min(groundY, transform.translation.y);
    groundY -= 0.05f;

    const float cx = initial.entityWorld.translation.x;
    const float cz = initial.entityWorld.translation.z;
    const float half = 12.0f;
    const std::vector<Vec3> vertices = {
        {cx - half, groundY, cz - half},
        {cx + half, groundY, cz - half},
        {cx + half, groundY, cz + half},
        {cx - half, groundY, cz + half},
    };
    const std::vector<std::uint32_t> indices = {0, 1, 2, 0, 2, 3};
    return world.setStaticMesh(vertices, indices, &error);
}

bool runVariant(const RagdollDescription& baseDescription,
                const RagdollInitialState& baseInitial,
                const Variant& variant,
                const std::string& outputDirectory,
                Summary& summary)
{
    RagdollSimulationConfig config{};
    config.gravity = physics_portable::makeVec3(0.0f, -19.62f, 0.0f);
    config.fixedTimestep = 0.01f;
    config.maxSubsteps = 4;
    config.contactOffset = 0.02f;
    config.restOffset = 0.0f;
    config.enableStabilization = true;
    config.solverPositionIterations = variant.solverPosition;
    config.solverVelocityIterations = variant.solverVelocity;
    config.promoteStaticRoot = true;
    config.staticRootDensity = variant.rootDensity;
    config.linearDamping = variant.linearDamping;
    config.angularDamping = variant.angularDamping;
    config.disableSelfCollision = variant.disableSelfCollision;
    config.collectDiagnostics = true;

    RagdollDescription description = baseDescription;
    if (variant.legacyRoot && !description.bodies.empty())
    {
        auto& root = description.bodies[description.rootBody];
        root.role = physics_portable::BodyRole::Dynamic;
        root.isFixed = false;
        root.mass = 0.5f;
        root.centerOfMass = Vec3{};
        root.inertia[0] = root.inertia[1] = root.inertia[2] = 0.0f;
        root.inertia[3] = root.inertia[4] = root.inertia[5] = 0.0f;
        root.inertia[0] = root.inertia[3] = root.inertia[5] = 0.01f;
    }

    PhysXRagdollWorld world(config);
    float groundY = 0.0f;
    std::string error;
    if (!buildGround(world, baseInitial, groundY, error))
    {
        std::cerr << variant.name << " ground failed: " << error << '\n';
        return false;
    }
    if (!world.create(description, baseInitial, &error))
    {
        std::cerr << variant.name << " create failed: " << error << '\n';
        return false;
    }

    if (variant.impulse)
    {
        if (variant.impulseBody >= baseDescription.bodies.size())
        {
            std::cerr << variant.name << " impulse body is out of range\n";
            return false;
        }
        physics_physx::BodyMassProperties mass;
        if (!world.massProperties(variant.impulseBody, mass, &error))
        {
            std::cerr << variant.name << " actual mass properties failed: " << error << '\n';
            return false;
        }
        Vec3 position = mass.actorWorld.translation;
        if (variant.impulsePoint == ImpulsePoint::CenterOfMass)
            position = mass.worldCenterOfMass;
        std::cout << "mass[" << variant.name << "] body=" << variant.impulseBody << " mass=" << mass.mass
                  << " actor=(" << mass.actorWorld.translation.x << ',' << mass.actorWorld.translation.y << ','
                  << mass.actorWorld.translation.z << ") com=(" << mass.worldCenterOfMass.x << ','
                  << mass.worldCenterOfMass.y << ',' << mass.worldCenterOfMass.z << ")\n";
        const Vec3 impulse{0.0f, 0.0f, variant.impulseMagnitude};
        if (!world.applyImpulse(variant.impulseBody, position, impulse, &error))
        {
            std::cerr << variant.name << " impulse failed: " << error << '\n';
            return false;
        }
    }

    const std::string csvPath = outputDirectory + "/stability_" + variant.name + ".csv";
    std::ofstream csv(csvPath);
    if (!csv)
    {
        std::cerr << variant.name << " cannot open CSV " << csvPath << '\n';
        return false;
    }
    csv << "step,kinetic,potential,total,max_angular,anchor_error,limit_error,contacts,found,persist,lost,active_pairs,"
           "contact_impulse,penetration,awake,dynamic,external_impulses\n";
    csv << std::setprecision(8);
    csv.flush();
    if (!csv)
    {
        std::cerr << variant.name << " CSV header write failed\n";
        return false;
    }

    const Vec3 reference{baseInitial.entityWorld.translation.x, groundY, baseInitial.entityWorld.translation.z};
    summary = Summary{};
    summary.name = variant.name;
    for (int step = 0; step < kSteps; ++step)
    {
        if (!world.stepFixed(1, &error))
        {
            std::cerr << variant.name << " step " << step << " failed: " << error << '\n';
            return false;
        }
        const RagdollDiagnostics diagnostics = world.diagnostics(reference);
        if (!finite(diagnostics.kineticEnergy) || !finite(diagnostics.potentialEnergy) ||
            !finite(diagnostics.totalEnergy) || !finite(diagnostics.maxAngularSpeed) ||
            !finite(diagnostics.maxJointAnchorError) || !finite(diagnostics.maxJointLimitError) ||
            !finite(diagnostics.maxPenetration) || !finite(diagnostics.totalContactImpulse))
        {
            std::cerr << variant.name << " produced a non-finite diagnostic at step " << step << '\n';
            return false;
        }

        summary.maxKinetic = std::max(summary.maxKinetic, diagnostics.kineticEnergy);
        summary.maxEnergy = std::max(summary.maxEnergy, diagnostics.totalEnergy);
        summary.maxAngular = std::max(summary.maxAngular, diagnostics.maxAngularSpeed);
        summary.maxAnchor = std::max(summary.maxAnchor, diagnostics.maxJointAnchorError);
        summary.maxLimit = std::max(summary.maxLimit, diagnostics.maxJointLimitError);
        summary.maxPenetration = std::max(summary.maxPenetration, diagnostics.maxPenetration);
        summary.maxContacts = std::max(summary.maxContacts, diagnostics.contactCount);
        summary.maxFound = std::max(summary.maxFound, diagnostics.contactFound);
        summary.maxPersist = std::max(summary.maxPersist, diagnostics.contactPersist);
        summary.maxLost = std::max(summary.maxLost, diagnostics.contactLost);
        summary.maxActivePairs = std::max(summary.maxActivePairs, diagnostics.activeContactPairs);
        summary.awakeAtEnd = diagnostics.awakeBodies;
        summary.totalMass = diagnostics.totalMass;
        summary.rootMass = diagnostics.rootMass;
        summary.finalKinetic = diagnostics.kineticEnergy;
        summary.stepsCompleted = static_cast<std::uint32_t>(step + 1);

        csv << step << ',' << diagnostics.kineticEnergy << ',' << diagnostics.potentialEnergy << ','
            << diagnostics.totalEnergy << ',' << diagnostics.maxAngularSpeed << ','
            << diagnostics.maxJointAnchorError << ',' << diagnostics.maxJointLimitError << ','
            << diagnostics.contactCount << ',' << diagnostics.contactFound << ',' << diagnostics.contactPersist << ','
            << diagnostics.contactLost << ',' << diagnostics.activeContactPairs << ','
            << diagnostics.totalContactImpulse << ',' << diagnostics.maxPenetration << ','
            << diagnostics.awakeBodies << ',' << diagnostics.dynamicBodies << ','
            << diagnostics.externalImpulses << '\n';
        if (!csv)
        {
            std::cerr << variant.name << " CSV write failed at step " << step << '\n';
            return false;
        }
    }
    if (summary.stepsCompleted != static_cast<std::uint32_t>(kSteps))
    {
        std::cerr << variant.name << " simulation did not complete all steps\n";
        return false;
    }
    csv.flush();
    if (!csv)
    {
        std::cerr << variant.name << " CSV flush failed\n";
        return false;
    }
    csv.close();
    if (csv.fail())
    {
        std::cerr << variant.name << " CSV close failed\n";
        return false;
    }

    summary.required = variant.required;
    summary.technicalOk = true;
    summary.acceptanceOk = summary.maxAnchor <= kMaxAnchorError && summary.maxLimit <= kMaxLimitError &&
                           summary.finalKinetic <= kMaxFinalKinetic && summary.awakeAtEnd == 0;
    if (variant.required && !summary.acceptanceOk)
    {
        std::cerr << variant.name << " acceptance thresholds failed: anchor=" << summary.maxAnchor
                  << " limit=" << summary.maxLimit << " final_ke=" << summary.finalKinetic
                  << " awake=" << summary.awakeAtEnd << '\n';
    }
    return true;
}

void printAnchors(const RagdollCaptureDocument& document, const char* label)
{
    float maxError = 0.0f;
    std::size_t maxJoint = 0;
    for (std::size_t index = 0; index < document.description.joints.size(); ++index)
    {
        const auto& joint = document.description.joints[index];
        if (joint.parentBody >= document.initial.bodyWorld.size() ||
            joint.childBody >= document.initial.bodyWorld.size())
            continue;
        const physics_portable::Transform parent =
            document.initial.bodyWorld[joint.parentBody] * joint.parentFrame;
        const physics_portable::Transform child =
            document.initial.bodyWorld[joint.childBody] * joint.childFrame;
        const Vec3 delta{parent.translation.x - child.translation.x, parent.translation.y - child.translation.y,
                         parent.translation.z - child.translation.z};
        const float error = std::sqrt(physics_portable::dot(delta, delta));
        std::cout << "anchor[" << label << "] joint=" << index << " parent=(" << parent.translation.x << ','
                  << parent.translation.y << ',' << parent.translation.z << ") child=(" << child.translation.x << ','
                  << child.translation.y << ',' << child.translation.z << ") error=" << error << '\n';
        if (error > maxError)
        {
            maxError = error;
            maxJoint = index;
        }
    }
    std::cout << "anchor[" << label << "] max=" << maxError << " joint=" << maxJoint << '\n';
}
} // namespace

int main(int argc, char** argv)
{
    const char* fixture = std::getenv("OGSR_RAGDOLL_FIXTURE");
    if (!fixture || !*fixture)
    {
        std::cerr << "stability: OGSR_RAGDOLL_FIXTURE is required\n";
        return 2;
    }
    std::ifstream input(fixture, std::ios::binary);
    if (!input)
    {
        std::cerr << "stability: cannot open required fixture " << fixture << '\n';
        return 2;
    }
    const std::string json((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    RagdollCaptureDocument document;
    std::string error;
    if (!physics_portable::parseRagdollCapture(json, document, &error))
    {
        std::cerr << "stability: fixture parse failed: " << error << '\n';
        return 2;
    }
    printAnchors(document, "live");

    if (const char* deadFixture = std::getenv("OGSR_RAGDOLL_FIXTURE_DEAD"); deadFixture && *deadFixture)
    {
        std::ifstream deadInput(deadFixture, std::ios::binary);
        if (!deadInput)
        {
            std::cerr << "stability: cannot open dead fixture " << deadFixture << '\n';
            return 2;
        }
        const std::string deadJson((std::istreambuf_iterator<char>(deadInput)), std::istreambuf_iterator<char>());
        RagdollCaptureDocument deadDocument;
        if (!physics_portable::parseRagdollCapture(deadJson, deadDocument, &error))
        {
            std::cerr << "stability: dead fixture parse failed: " << error << '\n';
            return 2;
        }
        printAnchors(deadDocument, "dead");
        std::cout << "dead fixture is diagnostic only; handoff support is limited to live animated poses\n";
    }

    const std::string outputDirectory = argc > 1 ? argv[1] : ".";
    const std::vector<Variant> variants = {
        {"Z_p21_synthetic_approx_body1_8_2", false, true, 1, ImpulsePoint::BodyOrigin, false, 0.0f, 0.0f, 8, 2,
         true},
        {"A_no_impulse", false, false, 0, ImpulsePoint::CenterOfMass, true, 0.05f, 0.2f, 8, 2, false},
        {"B_impulse_root_8_2", false, true, 0, ImpulsePoint::CenterOfMass, true, 0.05f, 0.2f, 8, 2, false},
        {"B_impulse_body1_8_2", false, true, 1, ImpulsePoint::CenterOfMass, true, 0.05f, 0.2f, 8, 2, false},
        {"C_self_collision_on_8_2", false, true, 0, ImpulsePoint::CenterOfMass, false, 0.05f, 0.2f, 8, 2, false},
        {"D_solver_16_2", true, true, 0, ImpulsePoint::CenterOfMass, true, 0.05f, 0.2f, 16, 2, false},
        {"D_solver_16_4", true, true, 0, ImpulsePoint::CenterOfMass, true, 0.05f, 0.2f, 16, 4, false},
        {"D_solver_16_4_40ns", false, true, 0, ImpulsePoint::CenterOfMass, true, 0.05f, 0.2f, 16, 4, false,
         300.0f, 40.0f},
        {"E_linear_damping_0", false, true, 0, ImpulsePoint::CenterOfMass, true, 0.0f, 0.2f, 16, 4, false},
        {"E_angular_damping_0_05", false, true, 0, ImpulsePoint::CenterOfMass, true, 0.05f, 0.05f, 16, 4, false},
        {"E_angular_damping_1_0", false, true, 0, ImpulsePoint::CenterOfMass, true, 0.05f, 1.0f, 16, 4, false},
        {"F_density_200", false, true, 0, ImpulsePoint::CenterOfMass, true, 0.05f, 0.2f, 16, 4, false, 200.0f},
        {"F_density_500", false, true, 0, ImpulsePoint::CenterOfMass, true, 0.05f, 0.2f, 16, 4, false, 500.0f},
        {"G_world_com_vs_body_origin", false, true, 0, ImpulsePoint::BodyOrigin, true, 0.05f, 0.2f, 16, 4, false},
    };

    std::vector<Summary> summaries;
    bool allTechnicalPassed = true;
    bool allRequiredAcceptancePassed = true;
    for (const Variant& variant : variants)
    {
        Summary summary;
        summary.name = variant.name;
        const bool technical = runVariant(document.description, document.initial, variant, outputDirectory, summary);
        summary.required = variant.required;
        summary.technicalOk = technical;
        if (!technical)
        {
            allTechnicalPassed = false;
            summary.acceptanceOk = false;
            if (variant.required)
                allRequiredAcceptancePassed = false;
        }
        else if (variant.required && !summary.acceptanceOk)
        {
            allRequiredAcceptancePassed = false;
        }
        summaries.push_back(summary);
    }

    std::cout << "variant,required,technical,acceptance,total_mass,root_mass,max_ke,final_ke,max_energy,max_angular,"
                 "max_anchor,max_limit,max_pen,max_contacts,max_found,max_persist,max_lost,max_active,awake_end,"
                 "steps\n";
    for (const Summary& summary : summaries)
    {
        std::cout << summary.name << ',' << (summary.required ? 1 : 0) << ','
                  << (summary.technicalOk ? 1 : 0) << ',' << (summary.acceptanceOk ? 1 : 0) << ','
                  << summary.totalMass << ',' << summary.rootMass << ',' << summary.maxKinetic << ','
                  << summary.finalKinetic << ',' << summary.maxEnergy << ',' << summary.maxAngular << ','
                  << summary.maxAnchor << ',' << summary.maxLimit << ',' << summary.maxPenetration << ','
                  << summary.maxContacts << ',' << summary.maxFound << ',' << summary.maxPersist << ','
                  << summary.maxLost << ',' << summary.maxActivePairs << ',' << summary.awakeAtEnd << ','
                  << summary.stepsCompleted << '\n';
    }
    return (allTechnicalPassed && allRequiredAcceptancePassed) ? 0 : 1;
}

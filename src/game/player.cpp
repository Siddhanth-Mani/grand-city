#include "game/player.h"
#include <algorithm>
namespace gc6 {
void Player::init(const City&) {
    ownedVehicle_ = std::make_unique<Vehicle>();
    ownedVehicle_->setPosition(Vec3(0, 0, 10), 0.0f);
    // Start on the pavement-side of the parked cars (4 m left/right at z=10),
    // close enough to press E and get in.
    character_.setPosition(Vec3(0, 0, 7));
}
void Player::setVehicleParams(const VehicleParams& p) {
    Vec3 pos = onFoot_ ? Vec3(0, 0, 10) : currentVehicle_->position();
    float yaw = onFoot_ ? 0.0f : currentVehicle_->yaw();
    ownedVehicle_ = std::make_unique<Vehicle>(p);
    ownedVehicle_->setPosition(pos, yaw);
    if (!onFoot_) currentVehicle_ = ownedVehicle_.get();
}
Vec3 Player::position() const {
    return onFoot_ ? character_.position() : currentVehicle_->position();
}
float Player::yaw() const {
    return onFoot_ ? character_.yaw() : currentVehicle_->yaw();
}
float Player::speedKmh() const {
    return onFoot_ ? 0.0f : currentVehicle_->speedKmh();
}
void Player::update(float dt, const InputState& input, const std::vector<AABB>& solids,
                    const std::vector<Vehicle*>& nearbyCars) {
    if (onFoot_) {
        Vec3 forward{std::sin(character_.yaw()), 0, std::cos(character_.yaw())};
        Vec3 right{std::cos(character_.yaw()), 0, -std::sin(character_.yaw())};
        Vec3 move{0, 0, 0};
        if (input.isDown(Key_W)) move += forward;
        if (input.isDown(Key_S)) move -= forward;
        if (input.isDown(Key_A)) move -= right;
        if (input.isDown(Key_D)) move += right;
        if (length(move) > 0.01f) move = normalize(move);
        character_.update(dt, move, input.isDown(Key_Shift), input.wasPressed(Key_Space), solids);
        if (input.wasPressed(Key_E)) {
            // Enter the nearest car within 6 m (about a lane and a half away).
            Vehicle* nearest = nullptr;
            float best = 6.0f;
            for (auto* car : nearbyCars) {
                if (!car) continue;
                float d = length(car->position() - character_.position());
                if (d < best) { best = d; nearest = car; }
            }
            if (nearest) {
                currentVehicle_ = nearest;
                onFoot_ = false;
            }
        }
    } else {
        float throttle = 0.0f, steer = 0.0f, brake = 0.0f;
        if (input.isDown(Key_W) || input.isDown(Key_Up)) throttle = 1.0f;
        if (input.isDown(Key_S) || input.isDown(Key_Down)) throttle = -0.6f;
        if (input.isDown(Key_A) || input.isDown(Key_Left)) steer = -1.0f;
        if (input.isDown(Key_D) || input.isDown(Key_Right)) steer = 1.0f;
        if (input.isDown(Key_Space)) brake = 1.0f;
        // Handbrake turn: Space at speed with steering locks the rear.
        currentVehicle_->setHandbrake(input.isDown(Key_Space) &&
                                     std::fabs(currentVehicle_->speed()) > 10.0f &&
                                     std::fabs(steer) > 0.5f);
        currentVehicle_->update(dt, throttle, steer, brake, solids);
        if (input.wasPressed(Key_E)) {
            character_.setPosition(currentVehicle_->position() + Vec3(2.5f, 0, 0));
            currentVehicle_ = nullptr;
            onFoot_ = true;
        }
    }
}
} // namespace gc6

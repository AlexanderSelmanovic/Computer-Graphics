#include "ParticleSystem.h"

// Methods
void ParticleSystem::kill(int id) {
	std::swap(particles[id], particles.back());
	particles.pop_back();
}

void ParticleSystem::spawn(Particle particle) {
	if (particles.size() < max_size)
		particles.push_back(particle);
}

void ParticleSystem::process_particles(float dt) {
	for (int i = 0; i < particles.size(); i++) {
		if (particles[i].lifetime + dt > particles[i].life_length)
			kill(i);
	}

	for (int i = 0; i < particles.size(); i++) {
		particles[i].lifetime += dt;
		particles[i].pos += particles[i].velocity * dt;
	}
}

#include "ParticleSystem.h"

/*
	beh�ver ta in particles p� n�got s�tt, antar att den ska finns i main p� n�got s�tt
*/


// Methods
void ParticleSystem::kill(int id) {
	particles.erase(particles.begin() + id);
}

void ParticleSystem::spawn(Particle particle) {
	particles.push_back(particle);
}

void ParticleSystem::process_particles(float dt) {
	for (int i = 0; i < particles.size(); i++) {
		Particle p = particles[i];
		if (p.lifetime + dt > p.life_length)
			kill(i);
	}

	for (int i = 0; i < particles.size(); i++) {
		Particle p = particles[i];
		p.lifetime = p.lifetime + dt;
		p.pos = p.pos + p.velocity * dt;
	}
}

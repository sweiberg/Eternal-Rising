# Eternal-Rising
An asymmetrical real-time strategy multiplayer game using Unreal Engine and Steamworks. Future plans will incorporate Flecs to manage larger hordes of zombies.

## Steamworks 
Includes a wrapper to handle server requests from Steam and a fully functioning server browser with working filters.

![alt text](Screenshots/1.gif)


## Gameplay
Joining a server gives the option to play as the Director or Survivor. The director can control hordes of zombies to attack the survivor player.
Zombies use flowfield pathfinding to navigate the city. Upon entering an idle stance they will use Boid's like pathfinding to hunt within a vicinity for the survivor. 
The director can move the zombies to head towards a destination or target the survivor specifically. 

Zombies also use vertex animations, via Unreal's AnimToTexture plugin to reduce draw calls by utilizing Instanced Static Meshes.

![alt text](Screenshots/2.gif)
![alt text](Screenshots/3.gif)
![alt text](Screenshots/4.gif)
![alt text](Screenshots/5.gif)

for img in *.png; do
   if [ -f "$img" ]; then
      # Place textures at VRAM (640,0) to avoid framebuffer overlap.
      # Framebuffers occupy (0,0)-(320,480), font at (960,0).
      png2tim -p 640 0 "$img";
      echo "Converted $img to TIM format.";
   fi;
done

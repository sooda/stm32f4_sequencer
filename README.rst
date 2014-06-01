softsynth for stm32f4discovery
------------------------------

This is a small software sound synthesizer for the stm32f4discovery board. Makefiles, init code and such are based on a simple `mp3 player`_ that seems to be based on ST's samples contained in the STM32CubeF4_ package, and some parts are copy-pasted around the net and the Cube samples.

.. _mp3 player: http://vedder.se/2012/07/play-mp3-on-the-stm32f4-discovery/
.. _STM32CubeF4: http://www.st.com/web/en/catalog/tools/PF259243

This was quickly written in about two days for the ELL-i `bare metal hackathon`_ in May 2014. Some explanations in Finnish in my blog_. Feel free to fork and experiment for fun. Do quote the original sources when publishing your changes.

.. _bare metal hackathon: http://ell-i.org/bare-metal-hackathon/
.. _blog: http://sooda.dy.fi/2014/6/1/ell-i-hackathon-ja-softasyna-armilla/

Set up potentiometers as voltage dividers to PA1 and PA2. Communication works with the USART on pins PD5:PD6. Sound outputs from the audio jack.

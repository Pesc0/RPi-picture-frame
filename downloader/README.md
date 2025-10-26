In this folder you should add 

- A file named constants.py containing the following variables:
    * `api_endpoint = "http://<proxy host : port>"`
      Url where the proxy component is running
    * `save_path = "/path/to/image/folder"`
      Where to save the images from immich. Should be the same folder where slideshow looks into
    * `img_width = 1920`
    * `img_height = 1080`
      Resolution of your display
    * `refresh_time_s = 60`
      How often to poll the proxy for the image list
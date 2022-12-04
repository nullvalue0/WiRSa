<?php

function file_get_contents_curl($url)
{
    $ch = curl_init();

	curl_setopt($ch, CURLOPT_USERAGENT, "WiRSa Version Check");
    curl_setopt($ch, CURLOPT_HEADER, 0);
    curl_setopt($ch, CURLOPT_RETURNTRANSFER, 1);
    curl_setopt($ch, CURLOPT_URL, $url);
    curl_setopt($ch, CURLOPT_FOLLOWLOCATION, 1);

    $data = curl_exec($ch);
    curl_close($ch);

    return $data;
}


$json = file_get_contents_curl('http://api.github.com/repos/nullvalue0/WiRSa/releases/latest');
$obj = json_decode($json);
echo $obj->{'tag_name'};


?>